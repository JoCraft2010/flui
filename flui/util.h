#include <stdlib.h>

#ifndef __flui_util_h
#define __flui_util_h

typedef struct pl_node {
	void *data;
	struct pl_node *prev;
	struct pl_node *next;
} pl_node;

typedef struct pointer_list {
	struct pl_node *head;
	struct pl_node *tail;
	size_t size;
} pointer_list;

pointer_list* create_pointer_list();
void destroy_pointer_list(pointer_list *list);
bool pointer_list_add_to_head(pointer_list *list, void *ptr);
bool pointer_list_remove(pointer_list *list, void *ptr);

#endif
