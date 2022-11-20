#include <stdlib.h>

#include "solution.h"

struct btree_node {
  bool leaf;
  unsigned int n;
  int *keys;
  struct btree_node **children;
};

struct btree {
  unsigned int btl;
  struct btree_node *root;
};

struct btree_node *btree_node_alloc(unsigned int btl, bool leaf) {
  struct btree_node *node = malloc(sizeof(struct btree_node));
  node->children = malloc(2 * btl * sizeof(struct btree_node *));
  node->keys = malloc(2 * btl * sizeof(int));
  node->leaf = leaf;
  node->n = 0;
  return node;
}

void btree_node_free(struct btree_node *node, bool rec) {
  if (rec && !node->leaf) {
    for (unsigned int i = 0; i < node->n; ++i) {
      btree_node_free(node->children[i], true);
    }
  }
  free(node->keys);
  free(node->children);
  free(node);
}

unsigned int btree_node_search_key(struct btree_node *node, int key) {
  unsigned int idx = 0;
  while (idx < node->n && node->keys[idx] < key) {
    ++idx;
  }
  return idx;
}

int btree_node_get_pred(struct btree_node *node, unsigned int idx) {
  struct btree_node *cur = node->children[idx];
  while (!cur->leaf) {
    cur = cur->children[cur->n];
  }
  return cur->keys[cur->n - 1];
}

int btree_node_get_suc(struct btree_node *node, unsigned int idx) {
  struct btree_node *cur = node->children[idx + 1];
  while (!cur->leaf) {
    cur = cur->children[0];
  }
  return cur->keys[0];
}

void btree_node_borrow_prev(struct btree_node *node, unsigned int idx) {
  struct btree_node *right = node->children[idx];
  struct btree_node *left = node->children[idx - 1];
  for (long i = right->n - 1; i >= 0; --i) {
    right->keys[i + 1] = right->keys[i];
  }
  if (!right->leaf) {
    for (long i = right->n; i >= 0; --i) {
      right->children[i + 1] = right->children[i];
    }
  }
  right->keys[0] = node->keys[idx - 1];
  if (!right->leaf) {
    right->children[0] = left->children[left->n];
  }
  node->keys[idx - 1] = left->keys[left->n - 1];
  ++right->n;
  --left->n;
}

void btree_node_borrow_next(struct btree_node *node, unsigned int idx) {
  struct btree_node *left = node->children[idx];
  struct btree_node *right = node->children[idx + 1];
  left->keys[left->n] = node->keys[idx];
  if (!left->leaf) {
    left->children[left->n + 1] = right->children[0];
  }
  node->keys[idx] = right->keys[0];
  for (unsigned int i = 1; i < right->n; ++i) {
    right->keys[i - 1] = right->keys[i];
  }
  if (!right->leaf) {
    for (unsigned int i = 1; i < right->n + 1; ++i) {
      right->children[i - 1] = right->children[i];
    }
  }
  ++left->n;
  --right->n;
}

void btree_node_merge(struct btree_node *node, unsigned int idx,
                      unsigned int btl) {
  struct btree_node *left = node->children[idx];
  struct btree_node *right = node->children[idx + 1];
  left->keys[btl - 1] = node->keys[idx];
  for (unsigned int i = 0; i < right->n; ++i) {
    left->keys[btl + i] = right->keys[i];
  }
  if (!left->leaf) {
    for (unsigned int i = 0; i < right->n + 1; ++i) {
      left->children[btl + i] = right->children[i];
    }
  }
  for (unsigned int i = idx + 1; i < node->n; ++i) {
    node->keys[i - 1] = node->keys[i];
  }
  for (unsigned int i = idx + 2; i < node->n + 1; ++i) {
    node->children[i - 1] = node->children[i];
  }
  left->n += right->n + 1;
  --node->n;
  btree_node_free(right, false);
}

void btree_node_fill(struct btree_node *node, unsigned int idx,
                     unsigned int btl) {
  if (idx && node->children[idx - 1]->n > btl - 1) {
    btree_node_borrow_prev(node, idx);
  } else if (idx < node->n && node->children[idx + 1]->n > btl - 1) {
    btree_node_borrow_next(node, idx);
  } else {
    btree_node_merge(node, idx == node->n ? idx - 1 : idx, btl);
  }
}

void btree_node_delete(struct btree_node *node, int key, unsigned int btl);

void btree_node_delete_leaf(struct btree_node *node, unsigned int idx) {
  for (unsigned int i = idx + 1; i < node->n; ++i) {
    node->keys[i - 1] = node->keys[i];
  }
  --node->n;
}

void btree_node_delete_nonleaf(struct btree_node *node, unsigned int idx,
                               unsigned int btl) {
  int key = node->keys[idx];
  if (node->children[idx]->n > btl - 1) {
    int pred = btree_node_get_pred(node, idx);
    node->keys[idx] = pred;
    btree_node_delete(node->children[idx], pred, btl);
  } else if (node->children[idx + 1]->n > btl - 1) {
    int suc = btree_node_get_suc(node, idx);
    node->keys[idx] = suc;
    btree_node_delete(node->children[idx + 1], suc, btl);
  } else {
    btree_node_merge(node, idx, btl);
    btree_node_delete(node->children[idx], key, btl);
  }
}

void btree_node_delete(struct btree_node *node, int key, unsigned int btl) {
  unsigned int idx = btree_node_search_key(node, key);
  if (idx < node->n && node->keys[idx] == key) {
    if (node->leaf) {
      btree_node_delete_leaf(node, idx);
    } else {
      btree_node_delete_nonleaf(node, idx, btl);
    }
  } else {
    if (node->leaf) {
      // no such key
      return;
    }
    bool in_last = idx == node->n;
    if (node->children[idx]->n < btl) {
      btree_node_fill(node, idx, btl);
    }
    if (in_last && idx > node->n) {
      btree_node_delete(node->children[idx - 1], key, btl);
    } else {
      btree_node_delete(node->children[idx], key, btl);
    }
  }
}

void btree_node_split(struct btree_node *node, struct btree_node *left,
                      long idx, unsigned int btl) {
  struct btree_node *new_right = btree_node_alloc(btl, left->leaf);
  new_right->n = btl - 1;
  for (long j = 0; j < btl - 1; ++j) {
    new_right->keys[j] = left->keys[j + btl];
  }
  if (!left->leaf) {
    for (long j = 0; j < btl; ++j) {
      new_right->children[j] = left->children[btl + j];
    }
  }
  left->n = btl - 1;
  for (long j = node->n; j >= idx + 1; --j) {
    node->children[j + 1] = node->children[j];
  }
  node->children[idx + 1] = new_right;
  for (long j = node->n - 1; j >= idx; --j) {
    node->keys[j + 1] = node->keys[j];
  }
  node->keys[idx] = left->keys[btl - 1];
  ++node->n;
}

void btree_node_insert_nonfull(struct btree_node *node, int key,
                               unsigned int btl) {
  long idx = (long)node->n - 1;
  if (node->leaf) {
    while (idx >= 0 && node->keys[idx] > key) {
      node->keys[idx + 1] = node->keys[idx];
      --idx;
    }
    node->keys[idx + 1] = key;
    ++node->n;
  } else {
    while (idx >= 0 && node->keys[idx] > key) {
      --idx;
    }
    if (node->children[idx + 1]->n == 2 * btl - 1) {
      btree_node_split(node, node->children[idx + 1], idx + 1, btl);
      if (node->keys[idx + 1] < key) {
        ++idx;
      }
    }
    btree_node_insert_nonfull(node->children[idx + 1], key, btl);
  }
}

struct btree_node *btree_node_search(struct btree_node *node, int key) {
  unsigned int i = 0;
  while (i < node->n && key > node->keys[i]) {
    ++i;
  }
  if (i < node->n && node->keys[i] == key) {
    return node;
  }
  if (node->leaf) {
    return NULL;
  }
  return btree_node_search(node->children[i], key);
}

struct btree *btree_alloc(unsigned int L) {
  struct btree *tree = malloc(sizeof(struct btree));
  tree->btl = L;
  tree->root = NULL;
  return tree;
}

void btree_free(struct btree *t) {
  if (t->root) {
    btree_node_free(t->root, true);
  }
  free(t);
}

void btree_insert(struct btree *t, int x) {
  if (!t->root) {
    t->root = btree_node_alloc(t->btl, true);
    t->root->keys[0] = x;
    t->root->n = 1;
  } else {
    if (t->root->n == 2 * t->btl - 1) {
      struct btree_node *new_node = btree_node_alloc(t->btl, false);
      new_node->children[0] = t->root;
      btree_node_split(new_node, t->root, 0, t->btl);
      unsigned int i = 0;
      if (new_node->keys[0] < x) {
        i = 1;
      }
      btree_node_insert_nonfull(new_node->children[i], x, t->btl);
      t->root = new_node;
    } else {
      btree_node_insert_nonfull(t->root, x, t->btl);
    }
  }
}

void btree_delete(struct btree *t, int x) {
  if (!t->root) {
    return;
  }
  btree_node_delete(t->root, x, t->btl);
  if (!t->root->n) {
    struct btree_node *old_root = t->root;
    if (t->root->leaf) {
      t->root = NULL;
    } else {
      t->root = t->root->children[0];
    }
    btree_node_free(old_root, false);
  }
}

bool btree_contains(struct btree *t, int x) {
  if (!t->root) {
    return false;
  }
  return btree_node_search(t->root, x) != NULL;
}

struct btree_iter_state {
  unsigned int idx;
  struct btree_node *node;
  struct btree_iter_state *prev_state;
};

struct btree_iter {
  struct btree_iter_state *cur_state;
};

struct btree_iter *btree_iter_start(struct btree *t) {
  struct btree_iter *iter = malloc(sizeof(struct btree_iter));
  if (!t->root) {
    iter->cur_state = NULL;
    return iter;
  }
  struct btree_iter_state *cur_state = malloc(sizeof(struct btree_iter_state));
  cur_state->idx = 0;
  cur_state->node = t->root;
  cur_state->prev_state = NULL;
  while (!cur_state->node->leaf) {
    struct btree_iter_state *new_state =
        malloc(sizeof(struct btree_iter_state));
    new_state->idx = 0;
    new_state->node = cur_state->node->children[0];
    new_state->prev_state = cur_state;
    cur_state = new_state;
  }
  iter->cur_state = cur_state;
  return iter;
}

void btree_iter_end(struct btree_iter *i) {
  struct btree_iter_state *cur_state = i->cur_state;
  while (cur_state) {
    struct btree_iter_state *prev_state = cur_state;
    cur_state = cur_state->prev_state;
    free(prev_state);
  }
  free(i);
}

bool btree_iter_next(struct btree_iter *i, int *x) {
  struct btree_iter_state *cur_state = i->cur_state;
  while (cur_state && cur_state->idx == cur_state->node->n) {
    struct btree_iter_state *old_state = cur_state;
    cur_state = cur_state->prev_state;
    free(old_state);
  }
  i->cur_state = cur_state;
  if (!cur_state) {
    return false;
  }
  *x = cur_state->node->keys[cur_state->idx];
  ++cur_state->idx;
  if (cur_state->node->leaf) {
    return true;
  }
  struct btree_iter_state *next = malloc(sizeof(struct btree_iter_state));
  next->idx = 0;
  next->node = cur_state->node->children[cur_state->idx];
  next->prev_state = cur_state;
  cur_state = next;
  while (!cur_state->node->leaf) {
    struct btree_iter_state *next = malloc(sizeof(struct btree_iter_state));
    next->node = cur_state->node->children[0];
    next->idx = 0;
    next->prev_state = cur_state;
    cur_state = next;
  }
  i->cur_state = cur_state;
  return true;
}
