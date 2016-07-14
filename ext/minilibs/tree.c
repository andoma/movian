#include <stdlib.h> /* malloc, free */
#include <string.h> /* strcmp, strdup */

#include "tree.h"

/* This is an AA-tree implementation. */

struct tree
{
	char *key;
	void *value;
	struct tree *left, *right;
	int level;
};

static struct tree sentinel = { "", NULL, &sentinel, &sentinel, 0 };

static struct tree *tree_make(const char *key, void *value)
{
	struct tree *node = malloc(sizeof(struct tree));
	node->key = strdup(key);
	node->value = value;
	node->left = node->right = &sentinel;
	node->level = 1;
	return node;
}

void *tree_lookup(struct tree *node, const char *key)
{
	if (node) {
		while (node != &sentinel) {
			int c = strcmp(key, node->key);
			if (c == 0)
				return node->value;
			else if (c < 0)
				node = node->left;
			else
				node = node->right;
		}
	}
	return NULL;
}

static struct tree *tree_skew(struct tree *node)
{
	if (node->left->level == node->level) {
		struct tree *save = node;
		node = node->left;
		save->left = node->right;
		node->right = save;
	}
	return node;
}

static struct tree *tree_split(struct tree *node)
{
	if (node->right->right->level == node->level) {
		struct tree *save = node;
		node = node->right;
		save->right = node->left;
		node->left = save;
		node->level++;
	}
	return node;
}

struct tree *tree_insert(struct tree *node, const char *key, void *value)
{
	if (node && node != &sentinel) {
		int c = strcmp(key, node->key);
		if (c < 0)
			node->left = tree_insert(node->left, key, value);
		else
			node->right = tree_insert(node->right, key, value);
		node = tree_skew(node);
		node = tree_split(node);
		return node;
	}
	return tree_make(key, value);
}

void tree_free(struct tree *node)
{
	if (node && node != &sentinel) {
		tree_free(node->left);
		tree_free(node->right);
		free(node->key);
		free(node);
	}
}

#ifdef TEST
#include <stdio.h>
static void tree_print(struct tree *node, int level)
{
	int i;
	if (node->left != &sentinel)
		tree_print(node->left, level + 1);
	for (i = 0; i < level; ++i)
		putchar(' ');
	printf("%s = %s (%d)\n", node->key, (char*)node->value, node->level);
	if (node->right != &sentinel)
		tree_print(node->right, level + 1);
}

int main(int argc, char **argv)
{
	struct tree *args = NULL;
	int i;
	for (i = 0; i < argc; ++i) {
		char buf[10];
		sprintf(buf, "%d", i);
		args = tree_insert(args, argv[i], strdup(buf));
	}
	tree_print(args, 0);
	return 0;
}
#endif
