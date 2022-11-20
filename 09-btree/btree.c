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

struct btree_node *btree_node_alloc(unsigned int btl) {
  struct btree_node *new_node = malloc(sizeof(struct btree_node));
  new_node->keys = malloc(2 * btl * sizeof(int));
  new_node->children = malloc(2 * btl * sizeof(struct btree_node));
  new_node->n = 0;
  new_node->leaf = true;
  return new_node;
}

struct btree *btree_alloc(unsigned int btl) {
  struct btree *tree = malloc(sizeof(struct btree));
  tree->btl = btl;
  tree->root = btree_node_alloc(btl);
  return tree;
}

void btree_node_free(struct btree_node *node, bool rec) {
  if (rec && !node->leaf) {
    for (unsigned int i = 0; i < node->n + 1; ++i) {
      btree_node_free(node->children[i], true);
    }
  }
  free(node->keys);
  free(node->children);
  free(node);
}

void btree_free(struct btree *t) {
  btree_node_free(t->root, true);
  free(t);
}

void btree_node_split(struct btree_node *node, int st_idx, int ste_idx,
                      unsigned int btl) {
  struct btree_node *left = node->children[st_idx];
  struct btree_node *right = btree_node_alloc(btl);
  right->leaf = left->leaf;
  for (int k = node->n - 1; k >= st_idx; --k) {
    node->keys[k + 1] = node->keys[k];
  }
  ++node->n;
  node->keys[st_idx] = left->keys[ste_idx];
  for (unsigned int k = st_idx + 1; k < node->n; ++k) {
    node->children[k + 1] = node->children[k];
  }
  node->children[st_idx + 1] = right;
  right->n = left->n - ste_idx - 1;
  left->n = ste_idx;
  for (unsigned int k = 0; k < right->n; ++k) {
    right->keys[k] = left->keys[k + ste_idx + 1];
  }
  for (unsigned int k = 0; k < right->n + 1; ++k) {
    right->children[k] = left->children[k + ste_idx + 1];
  }
}

void btree_insert_nonfull(struct btree_node *node, int x, unsigned int btl) {
  int i = node->n - 1;
  if (node->leaf) {
    while (i >= 0 && x < node->keys[i]) {
      node->keys[i + 1] = node->keys[i];
      --i;
    }
    node->keys[i + 1] = x;
    ++node->n;
  } else {
    while (i >= 0 && x < node->keys[i]) {
      --i;
    }
    ++i;
    if (node->children[i]->n == 2 * btl - 1) {
      btree_node_split(node, i, btl - 1, btl);
      if (x > node->keys[i]) {
        ++i;
      }
    }
    btree_insert_nonfull(node->children[i], x, btl);
  }
}

void btree_insert(struct btree *tree, int x) {
  struct btree_node *root = tree->root;
  if (root->n == 2 * tree->btl - 1) {
    struct btree_node *node = btree_node_alloc(tree->btl);
    tree->root = node;
    node->leaf = false;
    node->n = 0;
    node->children[0] = root;
    btree_node_split(node, 0, tree->btl - 1, tree->btl);
    btree_insert_nonfull(node, x, tree->btl);
  } else {
    btree_insert_nonfull(root, x, tree->btl);
  }
}

void btree_search_node(struct btree_node **node, long *st_idx, long *idx,
                       int x) {
  struct btree_node *parent = NULL;
  struct btree_node *cur = *node;
  long i = 0;
  long st_i = -1;
  while (true) {
    i = 0;
    for (; i < cur->n; ++i) {
      if (cur->keys[i] == x) {
        *node = parent;
        *st_idx = st_i;
        *idx = i;
        return;
      }
      if (cur->keys[i] > x) {
        if (cur->leaf) {
          *node = NULL;
          *st_idx = -1;
          *idx = -1;
          return;
        }
        break;
      }
    }
    parent = cur;
    st_i = i;
    cur = cur->children[i];
  }
}

void btree_node_delete_key_right(struct btree_node *node, unsigned int idx) {
  for (; idx < node->n - 1; ++idx) {
    node->keys[idx] = node->keys[idx + 1];
  }
}

void btree_node_delete_key_left(struct btree_node *node, unsigned int idx) {
  for (; idx > 0; --idx) {
    node->keys[idx] = node->keys[idx - 1];
  }
}

void btree_node_delete_child_right(struct btree_node *node, unsigned int idx) {
  for (; idx < node->n; ++idx) {
    node->children[idx] = node->children[idx + 1];
  }
}

struct btree_node *btree_node_search_min(struct btree_node *node) {
  struct btree_node *cur = node;
  struct btree_node *last_with_val = NULL;
  while (true) {
    if (cur->n > 0) {
      last_with_val = cur;
    }
    if (cur->leaf) {
      break;
    }
    cur = cur->children[0];
  }
  return last_with_val;
}

struct btree_node *btree_node_search_max(struct btree_node *node) {
  struct btree_node *cur = node;
  struct btree_node *last_with_val = NULL;
  while (true) {
    if (cur->n > 0) {
      last_with_val = cur;
    }
    if (cur->leaf) {
      break;
    }
    cur = cur->children[cur->n];
  }
  return last_with_val;
}

void btree_node_delete(struct btree_node *node, int x, unsigned int btl) {
  struct btree_node *parent = node;
  long idx = 0;
  long st_idx = 0;
  btree_search_node(&parent, &st_idx, &idx, x);
  if (idx < 0) {
    return;
  }
  struct btree_node *cur = parent ? parent->children[st_idx] : node;
  if (cur->leaf) {
    if (!parent || cur->n > btl - 1) {
      btree_node_delete_key_right(cur, idx);
      --cur->n;
      return;
    }
    long nbr_idx = st_idx ? st_idx - 1 : st_idx + 1;
    struct btree_node *nbr = parent->children[nbr_idx];
    if (nbr->n > btl - 1) {
      if (nbr_idx < st_idx) {
        btree_node_delete_key_left(cur, idx);
        cur->keys[0] = parent->keys[nbr_idx];
        parent->keys[nbr_idx] = nbr->keys[nbr->n - 1];
      } else {
        btree_node_delete_key_right(cur, idx);
        cur->keys[cur->n - 1] = parent->keys[st_idx];
        parent->keys[st_idx] = nbr->keys[0];
        btree_node_delete_key_right(nbr, 0);
      }
      --nbr->n;
    } else {
      long left_idx;
      long right_idx;
      if (nbr_idx > st_idx) {
        left_idx = st_idx;
        right_idx = nbr_idx;
      } else {
        left_idx = nbr_idx;
        right_idx = st_idx;
      }
      struct btree_node *left = parent->children[left_idx];
      struct btree_node *right = parent->children[right_idx];
      btree_node_delete_key_right(cur, idx);
      --cur->n;
      left->keys[left->n++] = parent->keys[left_idx];
      btree_node_delete_child_right(parent, right_idx);
      btree_node_delete_key_right(parent, left_idx);
      --parent->n;
      for (long i = 0; i < right->n; ++i) {
        left->keys[left->n++] = right->keys[i];
      }
      btree_node_free(right, false);
    }
  } else {
    if (cur->children[idx]->n > btl - 1) {
      struct btree_node *max_node = btree_node_search_max(cur->children[idx]);
      int max = max_node->keys[max_node->n - 1];
      btree_node_delete(cur, max, btl);
      cur->keys[idx] = max;
    } else if (cur->children[idx + 1]->n > btl - 1) {
      int min = btree_node_search_min(cur->children[idx + 1])->keys[0];
      btree_node_delete(cur, min, btl);
      cur->keys[idx] = min;
    } else {
      struct btree_node *left = cur->children[idx];
      struct btree_node *right = cur->children[idx + 1];
      left->keys[left->n++] = cur->keys[idx];
      for (long i = 0; i <= right->n; ++i) {
        left->children[i + left->n] = right->children[i];
      }
      for (long i = 0; i < right->n; ++i) {
        left->keys[left->n++] = right->keys[i];
      }
      btree_node_delete_key_right(cur, idx);
      --cur->n;
      btree_node_delete_child_right(cur, idx + 1);
      btree_node_free(right, false);
      btree_node_delete(left, x, btl);
    }
  }
}

void btree_delete(struct btree *tree, int x) {
  btree_node_delete(tree->root, x, tree->btl);
  if (tree->root->n == 0 && !tree->root->leaf) {
    struct btree_node *old_root = tree->root;
    tree->root = tree->root->children[0];
    btree_node_free(old_root, false);
  }
}

bool btree_contains(struct btree *t, int x) {
  if (!t->root) {
    return false;
  }
  struct btree_node *cur_node = t->root;
  while (true) {
    unsigned int idx = 0;
    for (; idx < cur_node->n; ++idx) {
      if (cur_node->keys[idx] == x) {
        return true;
      }
      if (cur_node->keys[idx] > x) {
        break;
      }
    }
    if (cur_node->leaf) {
      return false;
    }
    cur_node = cur_node->children[idx];
  }
}

struct btree_iter_node {
  unsigned int idx;
  struct btree_node *bnode;
  struct btree_iter_node *prev_iter_node;
};

struct btree_iter {
  struct btree_iter_node *cur;
};

struct btree_iter *btree_iter_start(struct btree *t) {
  struct btree_iter *iter = malloc(sizeof(struct btree_iter));
  struct btree_iter_node *iter_node = malloc(sizeof(struct btree_iter_node));
  iter_node->idx = 0;
  iter_node->bnode = t->root;
  iter_node->prev_iter_node = NULL;
  while (!iter_node->bnode->leaf) {
    struct btree_iter_node *new_iter_node =
        malloc(sizeof(struct btree_iter_node));
    new_iter_node->idx = 0;
    new_iter_node->bnode = iter_node->bnode->children[0];
    new_iter_node->prev_iter_node = iter_node;
    iter_node = new_iter_node;
  }
  iter->cur = iter_node;
  return iter;
}

void btree_iter_end(struct btree_iter *i) {
  struct btree_iter_node *iter_node = i->cur;
  while (iter_node) {
    struct btree_iter_node *prev_node = iter_node;
    iter_node = iter_node->prev_iter_node;
    free(prev_node);
  }
  free(i);
}

bool btree_iter_next(struct btree_iter *i, int *x) {
  struct btree_iter_node *cur = i->cur;
  while (cur && cur->idx == cur->bnode->n) {
    struct btree_iter_node *prev = cur;
    cur = cur->prev_iter_node;
    free(prev);
  }
  i->cur = cur;
  if (!cur) {
    return false;
  }
  *x = cur->bnode->keys[cur->idx];
  ++cur->idx;
  if (cur->bnode->leaf) {
    return true;
  }
  struct btree_iter_node *next = malloc(sizeof(struct btree_iter_node));
  next->idx = 0;
  next->bnode = cur->bnode->children[cur->idx];
  next->prev_iter_node = cur;
  cur = next;
  while (!cur->bnode->leaf) {
    struct btree_iter_node *next = malloc(sizeof(struct btree_iter_node));
    next->bnode = cur->bnode->children[0];
    next->idx = 0;
    next->prev_iter_node = cur;
    cur = next;
  }
  i->cur = cur;
  return true;
}
