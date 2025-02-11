#include "util.h"

pointer_list* create_pointer_list() {
	pointer_list *list = malloc(sizeof(struct pointer_list));
	list->head = NULL;
	list->tail = NULL;
	list->size = 0;
	return list;
}

void destroy_pointer_list(pointer_list *list) {
	if (!list) { return; }
	struct pl_node *current = list->head;
	while (current) {
		struct pl_node *next = current->next;
		free(current);
		current = next;
	}
	free(list);
}

bool pointer_list_add_to_head(pointer_list *list, void *ptr) {
	if (!list || !ptr) {
		return false;
	}

	struct pl_node *new_node = malloc(sizeof(struct pl_node));
	new_node->data = ptr;
	new_node->next = list->head;
	new_node->prev = NULL;

	if (list->head) {
		list->head->prev = new_node;
	}
	list->head = new_node;
	if (!list->tail) {
		list->tail = new_node;
	}

	list->size++;
	return true;
}

bool pointer_list_remove(pointer_list *list, void *ptr) {
	if (!list || !ptr) {
		return false;
	}

	for (struct pl_node *node = list->head; node != NULL; node = node->next) {
		if (node->data == ptr) {
			if (node->prev) {
				node->prev->next = node->next;
			}
			if (node->next) {
				node->next->prev = node->prev;
			}
			if (node == list->head) {
				list->head = node->next;
			}
			if (node == list->tail) {
				list->tail = node->prev;
			}
			free(node);
			list->size--;
			return true;
		}
	}
	return false;
}
