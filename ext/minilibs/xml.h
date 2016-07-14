#ifndef xml_h
#define xml_h

/* Parse UTF-8 string and return the XML root node, or NULL if there is a parse error. */
struct xml *xml_parse(char *buf, int preserve_white, char **error);

/* Free an XML node and all its children and siblings. */
void xml_free(struct xml *item);

/* Navigate the XML tree. */
struct xml *xml_prev(struct xml *item);
struct xml *xml_next(struct xml *item);
struct xml *xml_up(struct xml *item);
struct xml *xml_down(struct xml *item);

/* Return true if the tag name matches. */
int xml_is_tag(struct xml *item, const char *name);

/* Return tag name of XML node, or NULL if it's a text node. */
char *xml_tag(struct xml *item);

/* Return the value of an attribute of an XML node, or NULL if the attribute doesn't exist. */
char *xml_att(struct xml *item, const char *att);

/* Return the text content of an XML node, or NULL if the node is a tag. */
char *xml_text(struct xml *item);

/* Find the first sibling with the given tag name (may be the same node). */
struct xml *xml_find(struct xml *item, const char *tag);

/* Find the next sibling with the given tag name (never the same node). */
struct xml *xml_find_next(struct xml *item, const char *tag);

/* Find the first child with the given tag name. */
struct xml *xml_find_down(struct xml *item, const char *tag);

#endif
