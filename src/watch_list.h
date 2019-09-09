// Copyright snsinfu 2019.
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef FTRAP_WATCH_LIST_H
#define FTRAP_WATCH_LIST_H


struct watch_list {
    const char        *path;
    int                wd;
    struct watch_list *prev;
    struct watch_list *next;
};


// Function: watch_list_init
//
// Creates an empty watch list using given node as the sentinel.
//
// Parameters:
//   node - Pointer to a watch_list node.
//
// Returns:
//   Sentinel of the created empty list, i.e., the `node` argument.
//
struct watch_list *watch_list_init(struct watch_list *node);


// Function: watch_list_insert
//
// Inserts a node to the specified position of a watch list. The postcondition
// is `node->next == ent`.
//
// Parameters:
//   ent  - Node in a list where the new node is inserted before.
//   node - New node to insert.
//
// Returns:
//   The new node inserted, i.e., the `node` argument.
//
struct watch_list *watch_list_insert(struct watch_list *ent, struct watch_list *node);


// Function: watch_list_drop
//
// Drops a node from a watch list.
//
// Parameters:
//   ent - Node to drop.
//
// Returns:
//   The node dropped, i.e., the `ent` argument.
//
struct watch_list *watch_list_drop(struct watch_list *ent);


// Function: watch_list_find
//
// Finds a node by wd value.
//
// Parameters:
//   list - Sentinel node of the list to find a node in.
//   wd   - wd value to identify a node.
//
// Returns:
//   First node having the given wd, or NULL if no such node exists.
//
struct watch_list *watch_list_find(struct watch_list *list, int wd);


// Function: watch_list_nonempty
//
// Parameters:
//   list - Sentinel node of a list.
//
// Returns:
//   1 if the list contains at least one element, or 0 if not.
//
int watch_list_nonempty(struct watch_list *list);


#endif
