
/* PEG Markdown Highlight
 * Copyright 2011 Ali Rantakari -- http://hasseg.org
 * Licensed under the GPL2+ and MIT licenses (see LICENSE for more info).
 * 
 * styleparser.leg
 * 
 */

#include "styleparser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>


// vasprintf is not in the C standard nor in POSIX so we provide our own
int our_vasprintf(char **strptr, const char *fmt, va_list argptr)
{
    int ret;
    va_list argptr2;
    *strptr = NULL;
    
    va_copy(argptr2, argptr);
    ret = vsnprintf(NULL, 0, fmt, argptr2);
    if (ret <= 0)
        return ret;
    
    *strptr = (char *)malloc(ret+1);
    if (*strptr == NULL)
        return -1;
    
    va_copy(argptr2, argptr);
    ret = vsnprintf(*strptr, ret+1, fmt, argptr2);
    
    return ret;
}



// Set custom symbol name prefix to avoid name collisions with the main
// PEG Markdown Highlight parser:
#define YY_NAME(N) style_yy##N


// Parsing context data
typedef struct
{
    char *input;
    int pos;
    void (*error_callback)(char*,void*);
    void *error_callback_context;
    int styles_pos;
    style_collection *styles;
} style_parser_data;

typedef struct sem_value
{
    char *name;
    char *value;
    struct sem_value *next;
} sem_value;

sem_value *new_sem_value(char *name, char *value)
{
    sem_value *v = (sem_value *)malloc(sizeof(sem_value));
    v->name = name;
    v->value = value;
    v->next = NULL;
    return v;
}

void free_sem_values(sem_value *list)
{
    sem_value *cur = list;
    while (cur != NULL)
    {
        if (cur->name != NULL) free(cur->name);
        if (cur->value != NULL) free(cur->value);
        sem_value *this = cur;
        cur = cur->next;
        free(this);
    }
}

/*
static sem_value *cons(sem_value *elem, sem_value *list)
{
    elem->next = list;
    return elem;
}
*/

void report_error(style_parser_data *p_data, char *str, ...)
{
    if (p_data->error_callback == NULL)
        return;
    va_list argptr;
    va_start(argptr, str);
    char *errmsg;
    our_vasprintf(&errmsg, str, argptr);
    va_end(argptr);
    p_data->error_callback(errmsg, p_data->error_callback_context);
    free(errmsg);
}



char *trim_str(char *str)
{
    while (isspace(*str))
        str++;
    if (*str == '\0')
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end))
        end--;
    *(end+1) = '\0';
    return str;
}

char *strcpy_lower(char *str)
{
    char *low = strdup(str);
    int i;
    int len = strlen(str);
    for (i = 0; i < len; i++)
        *(low+i) = tolower(*(low+i));
    return low;
}

char *standardize_str(char *str)
{
    return strcpy_lower(trim_str(str));
}




attr_argb_color *new_argb_color(int r, int g, int b, int a)
{
    attr_argb_color *c = (attr_argb_color *)malloc(sizeof(attr_argb_color));
    c->red = r; c->green = g; c->blue = b; c->alpha = a;
    return c;
}
attr_argb_color *new_argb_from_hex(long hex, bool has_alpha)
{
    // 0xaarrggbb
    int a = has_alpha ? ((hex >> 24) & 0xFF) : 255;
    int r = ((hex >> 16) & 0xFF);
    int g = ((hex >> 8) & 0xFF);
    int b = (hex & 0xFF);
    return new_argb_color(r,g,b,a);
}
attr_argb_color *new_argb_from_hex_str(style_parser_data *p_data, char *str)
{
    // "aarrggbb"
    int len = strlen(str);
    if (len != 6 && len != 8) {
        report_error(p_data,
                     "Value '%s' is not a valid color value: it should be a "
                     "hexadecimal number, 6 or 8 characters long.",
                     str);
        return NULL;
    }
    char *endptr = NULL;
    long num = strtol(str, &endptr, 16);
    if (*endptr != '\0') {
        report_error(p_data,
                     "Value '%s' is not a valid color value: the character "
                     "'%c' is invalid. The color value should be a hexadecimal "
                     "number, 6 or 8 characters long.",
                     str, *endptr);
        return NULL;
    }
    return new_argb_from_hex(num, (len == 8));
}

attr_value *new_attr_value()
{
    return (attr_value *)malloc(sizeof(attr_value));
}

attr_font_styles *new_font_styles()
{
    attr_font_styles *ret = (attr_font_styles *)
                            malloc(sizeof(attr_font_styles));
    ret->italic = false;
    ret->bold = false;
    ret->underlined = false;
    return ret;
}

style_attribute *new_attr(char *name, attr_type type)
{
    style_attribute *attr = (style_attribute *)malloc(sizeof(style_attribute));
    attr->name = strdup(name);
    attr->type = type;
    attr->next = NULL;
    return attr;
}

void free_style_attributes(style_attribute *list)
{
    style_attribute *cur = list;
    while (cur != NULL)
    {
        if (cur->name != NULL)
            free(cur->name);
        if (cur->value != NULL)
        {
            if (cur->type == attr_type_foreground_color
                || cur->type == attr_type_background_color
                || cur->type == attr_type_caret_color)
                free(cur->value->argb_color);
            else if (cur->type == attr_type_font_family)
                free(cur->value->font_family);
            else if (cur->type == attr_type_font_style)
                free(cur->value->font_styles);
            else if (cur->type == attr_type_other)
                free(cur->value->string);
            free(cur->value);
        }
        style_attribute *this = cur;
        cur = cur->next;
        free(this);
    }
}






char **get_element_type_names()
{
    static char **elem_type_names = NULL;
    if (elem_type_names == NULL)
    {
        elem_type_names = (char **)malloc(sizeof(char*) * pmh_NUM_LANG_TYPES);
        int i;
        for (i = 0; i < pmh_NUM_LANG_TYPES; i++)
            elem_type_names[i] = NULL;
        elem_type_names[pmh_LINK] = "LINK";
        elem_type_names[pmh_AUTO_LINK_URL] = "AUTO_LINK_URL";
        elem_type_names[pmh_AUTO_LINK_EMAIL] = "AUTO_LINK_EMAIL";
        elem_type_names[pmh_IMAGE] = "IMAGE";
        elem_type_names[pmh_CODE] = "CODE";
        elem_type_names[pmh_HTML] = "HTML";
        elem_type_names[pmh_HTML_ENTITY] = "HTML_ENTITY";
        elem_type_names[pmh_EMPH] = "EMPH";
        elem_type_names[pmh_STRONG] = "STRONG";
        elem_type_names[pmh_LIST_BULLET] = "LIST_BULLET";
        elem_type_names[pmh_LIST_ENUMERATOR] = "LIST_ENUMERATOR";
        elem_type_names[pmh_COMMENT] = "COMMENT";
        elem_type_names[pmh_H1] = "H1";
        elem_type_names[pmh_H2] = "H2";
        elem_type_names[pmh_H3] = "H3";
        elem_type_names[pmh_H4] = "H4";
        elem_type_names[pmh_H5] = "H5";
        elem_type_names[pmh_H6] = "H6";
        elem_type_names[pmh_BLOCKQUOTE] = "BLOCKQUOTE";
        elem_type_names[pmh_VERBATIM] = "VERBATIM";
        elem_type_names[pmh_HTMLBLOCK] = "HTMLBLOCK";
        elem_type_names[pmh_HRULE] = "HRULE";
        elem_type_names[pmh_REFERENCE] = "REFERENCE";
        elem_type_names[pmh_NOTE] = "NOTE";
    }
    return elem_type_names;
}

pmh_element_type element_type_from_name(char *name)
{
    char **elem_type_names = get_element_type_names();
    
    int i;
    for (i = 0; i < pmh_NUM_LANG_TYPES; i++)
    {
        char *i_name = elem_type_names[i];
        if (i_name == NULL)
            continue;
        if (strcmp(i_name, name) == 0)
            return i;
    }
    
    return pmh_NO_TYPE;
}

char *element_name_from_type(pmh_element_type type)
{
    char **elem_type_names = get_element_type_names();
    char* ret = elem_type_names[type];
    if (ret == NULL)
        return "unknown type";
    return ret;
}


#define IF_ATTR_NAME(x) if (strcmp(x, name) == 0)
attr_type attr_type_from_name(char *name)
{
    IF_ATTR_NAME("color") return attr_type_foreground_color;
    else IF_ATTR_NAME("foreground") return attr_type_foreground_color;
    else IF_ATTR_NAME("foreground-color") return attr_type_foreground_color;
    else IF_ATTR_NAME("background") return attr_type_background_color;
    else IF_ATTR_NAME("background-color") return attr_type_background_color;
    else IF_ATTR_NAME("caret") return attr_type_caret_color;
    else IF_ATTR_NAME("caret-color") return attr_type_caret_color;
    else IF_ATTR_NAME("font-size") return attr_type_font_size_pt;
    else IF_ATTR_NAME("font-family") return attr_type_font_family;
    else IF_ATTR_NAME("font-style") return attr_type_font_style;
    return attr_type_other;
}

char *attr_name_from_type(attr_type type)
{
    switch (type)
    {
        case attr_type_foreground_color:
            return "foreground-color"; break;
        case attr_type_background_color:
            return "background-color"; break;
        case attr_type_caret_color:
            return "caret-color"; break;
        case attr_type_font_size_pt:
            return "font-size"; break;
        case attr_type_font_family:
            return "font-family"; break;
        case attr_type_font_style:
            return "font-style"; break;
        default:
            return "unknown";
    }
}


typedef struct multi_value
{
    char *value;
    size_t length;
    struct multi_value *next;
} multi_value;

multi_value *split_multi_value(char *input, char separator)
{
    multi_value *head = NULL;
    multi_value *tail = NULL;
    
    char *c = input;
    while (*c != '\0')
    {
        size_t i;
        for (i = 0; (*(c+i) != '\0' && *(c+i) != separator); i++);
        
        multi_value *mv = (multi_value *)malloc(sizeof(multi_value));
        mv->value = (char *)malloc(sizeof(char)*i + 1);
        mv->length = i;
        *mv->value = '\0';
        strncat(mv->value, c, i);
        
        if (head == NULL) {
            head = mv;
            tail = mv;
        } else {
            tail->next = mv;
            tail = mv;
        }
        
        if (*(c+i) == separator)
            i++;
        c += i;
    }
    
    return head;
}

void free_multi_value(multi_value *val)
{
    multi_value *cur = val;
    while (cur != NULL)
    {
        multi_value *this = cur;
        multi_value *next = this->next;
        free(this->value);
        free(this);
        cur = next;
    }
}




#define EQUALS(a,b) (strcmp(a, b) == 0)
style_attribute *interpret_attributes(style_parser_data *p_data,
                                      pmh_element_type lang_element_type,
                                      sem_value *raw_attributes)
{
    style_attribute *attrs = NULL;
    
    sem_value *cur = raw_attributes;
    while (cur != NULL)
    {
        attr_type atype = attr_type_from_name(cur->name);
        style_attribute *attr = new_attr(cur->name, atype);
        attr->lang_element_type = lang_element_type;
        attr->value = new_attr_value();
        
        if (atype == attr_type_foreground_color
            || atype == attr_type_background_color
            || atype == attr_type_caret_color)
        {
            char *hexstr = trim_str(cur->value);
            // new_argb_from_hex_str() reports conversion errors
            attr->value->argb_color = new_argb_from_hex_str(p_data, hexstr);
            if (attr->value->argb_color == NULL) {
                free_style_attributes(attr);
                attr = NULL;
            }
        }
        else if (atype == attr_type_font_size_pt)
        {
            char *endptr = NULL;
            attr->value->font_size_pt = (int)strtol(cur->value, &endptr, 10);
            if (endptr == cur->value) {
                report_error(p_data, "Value '%s' is invalid for attribute '%s'",
                             cur->value, cur->name);
                free_style_attributes(attr);
                attr = NULL;
            }
        }
        else if (atype == attr_type_font_family)
        {
            attr->value->font_family = strdup(cur->value);
        }
        else if (atype == attr_type_font_style)
        {
            attr->value->font_styles = new_font_styles();
            multi_value *values = split_multi_value(cur->value, ',');
            multi_value *value_cur = values;
            while (value_cur != NULL)
            {
                char *standardized_value = standardize_str(value_cur->value);
                
                if (EQUALS(standardized_value, "italic"))
                    attr->value->font_styles->italic = true;
                else if (EQUALS(standardized_value, "bold"))
                    attr->value->font_styles->bold = true;
                else if (EQUALS(standardized_value, "underlined"))
                    attr->value->font_styles->underlined = true;
                else {
                    report_error(p_data, "Value '%s' is invalid for attribute '%s'",
                                 standardized_value, cur->name);
                }
                
                free(standardized_value);
                value_cur = value_cur->next;
            }
            free_multi_value(values);
        }
        else if (atype == attr_type_other)
        {
            attr->value->string = strdup(cur->value);
        }
        
        if (attr != NULL) {
            // add to linked list
            attr->next = attrs;
            attrs = attr;
        }
        
        cur = cur->next;
    }
    
    return attrs;
}


void interpret_and_add_style(style_parser_data *p_data,
                             char *element_type_name,
                             sem_value *raw_attributes)
{
    bool isEditorType = false;
    bool isCurrentLineType = false;
    bool isSelectionType = false;
    pmh_element_type type = element_type_from_name(element_type_name);
    if (type == pmh_NO_TYPE)
    {
        if (EQUALS(element_type_name, "editor"))
            isEditorType = true, type = pmh_NO_TYPE;
        else if (EQUALS(element_type_name, "editor-current-line"))
            isCurrentLineType = true, type = pmh_NO_TYPE;
        else if (EQUALS(element_type_name, "editor-selection"))
            isSelectionType = true, type = pmh_NO_TYPE;
        else {
            report_error(p_data,
                "Style rule '%s' is not a language element type name or "
                "one of the following: 'editor', 'editor-current-line', "
                "'editor-selection'",
                element_type_name);
            return;
        }
    }
    style_attribute *attrs = interpret_attributes(p_data, type, raw_attributes);
    if (isEditorType)
        p_data->styles->editor_styles = attrs;
    else if (isCurrentLineType)
        p_data->styles->editor_current_line_styles = attrs;
    else if (isSelectionType)
        p_data->styles->editor_selection_styles = attrs;
    else
        p_data->styles->element_styles[(p_data->styles_pos)++] = attrs;
}







bool char_is_whitespace(char c)
{
    return (c == ' ' || c == '\t');
}

bool char_begins_linecomment(char c)
{
    return (c == '#');
}

bool line_is_comment(multi_value *line)
{
    char *c;
    for (c = line->value; *c != '\0'; c++)
    {
        if (!char_is_whitespace(*c))
            return char_begins_linecomment(*c);
    }
    return false;
}

bool line_is_empty(multi_value *line)
{
    char *c;
    for (c = line->value; *c != '\0'; c++)
    {
        if (!char_is_whitespace(*c))
            return false;
    }
    return true;
}



typedef struct block
{
    multi_value *lines;
    struct block *next;
} block;

block *new_block()
{
    block *ret = (block *)malloc(sizeof(block));
    ret->next = NULL;
    ret->lines = NULL;
    return ret;
}

void free_blocks(block *val)
{
    block *cur = val;
    while (cur != NULL)
    {
        block *this = cur;
        block *next = this->next;
        free_multi_value(this->lines);
        free(this);
        cur = next;
    }
}

block *get_blocks(char *input)
{
    block *head = NULL;
    block *tail = NULL;
    block *current_block = NULL;
    
    multi_value *lines = split_multi_value(input, '\n');
    multi_value *previous_line = NULL;
    multi_value *line_cur = lines;
    while (line_cur != NULL)
    {
        if (line_is_empty(line_cur))
        {
            if (current_block != NULL)
            {
                // terminate block
                tail->next = current_block;
                tail = current_block;
                current_block = NULL;
                previous_line->next = NULL;
            }
        }
        else if (!line_is_comment(line_cur))
        {
            if (current_block == NULL)
            {
                // start block
                current_block = new_block();
                current_block->lines = line_cur;
                if (previous_line != NULL)
                    previous_line->next = NULL;
            }
            if (head == NULL) {
                head = current_block;
                tail = current_block;
            }
        }
        previous_line = line_cur;
        multi_value *next = line_cur->next;
        line_cur = next;
    }
    
    if (current_block != NULL)
    {
        // terminate block
        tail->next = current_block;
        tail = current_block;
        current_block = NULL;
    }
    
    return head;
}


#define IS_ASSIGNMENT_OP(c)         ((c) == ':' || (c) == '=')
#define IS_STYLE_RULE_NAME_CHAR(c)  \
    ( (c) != '\0' && !isspace(c) && !IS_ASSIGNMENT_OP(c) )

char *get_style_rule_name(char *line)
{
    size_t start_index;
    for (start_index = 0;
         (*(line+start_index) != '\0' && isspace(*(line+start_index)));
         start_index++);
    
    size_t end_index;
    for (end_index = start_index;
         IS_STYLE_RULE_NAME_CHAR(*(line + end_index));
         end_index++);
    
    size_t len = end_index - start_index;
    char *ret = (char *)malloc(sizeof(char)*len + 1);
    *ret = '\0';
    strncat(ret, (line + start_index), len);
    
    return ret;
}


void _sty_parse(style_parser_data *p_data)
{
    // TODO: standardize line endings to \n
    
    block *blocks = get_blocks(p_data->input);
    
    block *block_cur = blocks;
    while (block_cur != NULL)
    {
        printf("Block:\n");
        multi_value *header_line = block_cur->lines;
        if (header_line != NULL)
        {
            printf("  Head line (len %ld): '%s'\n",
                   header_line->length, header_line->value);
            char *style_rule_name = get_style_rule_name(header_line->value);
            printf("  Style rule name: '%s'\n", style_rule_name);
            
            multi_value *attr_line_cur = header_line->next;
            if (attr_line_cur == NULL)
                report_error(p_data,
                             "No style attributes defined for style rule '%s'",
                             style_rule_name);
            
            while (attr_line_cur != NULL)
            {
                printf("  Attr line (len %ld): '%s'\n",
                       attr_line_cur->length, attr_line_cur->value);
                attr_line_cur = attr_line_cur->next;
            }
            
            free(style_rule_name);
        }
        block_cur = block_cur->next;
    }
    
    free_blocks(blocks);
}



style_collection *new_style_collection()
{
    style_collection *sc = (style_collection *)
                           malloc(sizeof(style_collection));
    
    sc->element_styles = (style_attribute**)
                         malloc(sizeof(style_attribute*) * pmh_NUM_LANG_TYPES);
    int i;
    for (i = 0; i < pmh_NUM_LANG_TYPES; i++)
        sc->element_styles[i] = NULL;
    
    sc->editor_styles = NULL;
    sc->editor_current_line_styles = NULL;
    sc->editor_selection_styles = NULL;
    
    return sc;
}

void free_style_collection(style_collection *coll)
{
    free_style_attributes(coll->editor_styles);
    free_style_attributes(coll->editor_current_line_styles);
    free_style_attributes(coll->editor_selection_styles);
    int i;
    for (i = 0; i < pmh_NUM_LANG_TYPES; i++)
        free_style_attributes(coll->element_styles[i]);
    free(coll->element_styles);
    free(coll);
}

style_parser_data *new_style_parser_data(char *input)
{
    style_parser_data *p_data = (style_parser_data*)
                                malloc(sizeof(style_parser_data));
    p_data->input = input;
    p_data->pos = 0;
    p_data->styles_pos = 0;
    p_data->styles = new_style_collection();
    return p_data;
}

style_collection *parse_styles(char *input, void(*error_callback)(char*,void*),
                               void *error_callback_context)
{
    style_parser_data *p_data = new_style_parser_data(input);
    p_data->error_callback = error_callback;
    p_data->error_callback_context = error_callback_context;
    
    _sty_parse(p_data);
    
    style_collection *ret = p_data->styles;
    free(p_data);
    return ret;
}


