#ifndef CIRCULAR_LIST_H
#define CIRCULAR_LIST_H

struct circular_list {
	struct circular_list *next;
	struct circular_list *prev;
};

#define CIRCULAR_LIST_HEAD_INIT(l) { &(l), &(l) }

static inline void circular_list_init(struct circular_list *list)
{
	list->next = list;
	list->prev = list;
}

static inline void circular_list_add(struct circular_list *list, struct circular_list *item)
{
	item->next = list->next;
	item->prev = list;
	list->next->prev = item;
	list->next = item;
}

static inline void circular_list_add_tail(struct circular_list *list, struct circular_list *item)
{
	circular_list_add(list->prev, item);
}

static inline void circular_list_del(struct circular_list *item)
{
	item->next->prev = item->prev;
	item->prev->next = item->next;
	item->next = NULL;
	item->prev = NULL;
}

static inline int circular_list_empty(const struct circular_list *list)
{
	return list->next == list;
}

static inline unsigned int circular_list_len(const struct circular_list *list)
{
	struct circular_list *item;
	int count = 0;
	for (item = list->next; item != list; item = item->next)
		count++;
	return count;
}

#ifndef offsetof
#define offsetof(type, member) ((long) &((type *) 0)->member)
#endif

#define circular_list_entry(item, type, member) \
	((type *) ((char *) item - offsetof(type, member)))

#define circular_list_first(list, type, member) \
	(circular_list_empty((list)) ? NULL : \
	 circular_list_entry((list)->next, type, member))

#define circular_list_last(list, type, member) \
	(circular_list_empty((list)) ? NULL : \
	 circular_list_entry((list)->prev, type, member))

#define circular_list_for_each(item, list, type, member) \
	for (item = circular_list_entry((list)->next, type, member); \
	     &item->member != (list); \
	     item = circular_list_entry(item->member.next, type, member))

#define circular_list_for_each_safe(item, n, list, type, member) \
	for (item = circular_list_entry((list)->next, type, member), \
		     n = circular_list_entry(item->member.next, type, member); \
	     &item->member != (list); \
	     item = n, n = circular_list_entry(n->member.next, type, member))

#define circular_list_for_each_reverse(item, list, type, member) \
	for (item = circular_list_entry((list)->prev, type, member); \
	     &item->member != (list); \
	     item = circular_list_entry(item->member.prev, type, member))

#define DEFINE_CIRCULAR_LIST(name) \
	struct circular_list name = { &(name), &(name) }

#endif /* CIRCULAR_LIST_H */
