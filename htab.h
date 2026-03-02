#pragma once

#include "utils.h"

// intrusive data structure
struct HNode {
  HNode *next = nullptr;
  u64 hcode = 0; // hash value
};

struct HTab {
  HNode **tab = nullptr; // array of slots

  size_t mask = 0; // array size - 1
  size_t size = 0; // number of keys
};

struct HMap {
  HTab newer;
  HTab older;
  size_t migrate_pos = 0;
};

constexpr size_t kMaxLoadFactor = 8;
constexpr size_t kRehashingWork = 128; // constant work

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);
void hm_clear(HMap *hmap);
size_t hm_size(HMap *hmap);
void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg);
