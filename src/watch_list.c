// Copyright snsinfu 2019.
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <stddef.h>

#include "watch_list.h"


struct watch_list *watch_list_init(struct watch_list *node)
{
    node->prev = node;
    node->next = node;
    return node;
}

struct watch_list *watch_list_insert(struct watch_list *ent, struct watch_list *node)
{
    // Precondition:  prev -> ent
    // Postcondition: prev -> node -> ent
    struct watch_list *prev = ent->prev;
    node->prev = prev;
    node->next = ent;
    prev->next = node;
    ent->prev = node;
    return node;
}

struct watch_list *watch_list_drop(struct watch_list *ent)
{
    struct watch_list *prev = ent->prev;
    struct watch_list *next = ent->next;
    prev->next = next;
    next->prev = prev;
    return ent;
}

struct watch_list *watch_list_find(struct watch_list *list, int wd)
{
    for (struct watch_list *ent = list->next; ent != list; ent = ent->next) {
        if (ent->wd == wd) {
            return ent;
        }
    }
    return NULL;
}

int watch_list_nonempty(struct watch_list *list)
{
    return list->next != list;
}
