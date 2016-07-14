#ifndef tree_h
#define tree_h

struct tree *tree_insert(struct tree *node, const char *key, void *value);
void *tree_lookup(struct tree *node, const char *key);
void tree_free(struct tree *node);

#endif
