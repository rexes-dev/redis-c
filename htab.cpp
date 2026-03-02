#include "htab.h"

static void h_init(HTab *htab, size_t n) {
  htab->tab = static_cast<HNode **>(calloc(n, sizeof(HNode *)));
  htab->mask = n - 1;
  htab->size = 0;
}

static HNode *h_detach(HTab *htab, HNode **from) {
  HNode *node = *from;
  // update &htab->tab[pos] or &parent->next
  *from = node->next;
  htab->size--;
  return node;
}

static void h_insert(HTab *htab, HNode *node) {
  const size_t pos = node->hcode & htab->mask;
  node->next = htab->tab[pos];
  htab->tab[pos] = node;
  htab->size++;
}

static HNode **h_lookup(HTab *htab, HNode *key, bool (*eq)(HNode *, HNode *)) {
  if (!htab->tab)
    return nullptr;

  const size_t pos = key->hcode & htab->mask;
  HNode **from = &htab->tab[pos];
  for (HNode *cur; (cur = *from) != nullptr; from = &cur->next) {
    if (cur->hcode == key->hcode && eq(cur, key)) {
      return from;
    }
  }
  return nullptr;
}

static void hm_trigger_rehashing(HMap *hmap) {
  hmap->older = hmap->newer;
  h_init(&hmap->newer, (hmap->newer.mask + 1) * 2);
  hmap->migrate_pos = 0;
}

static void hm_help_rehashing(HMap *hmap) {
  size_t nwork = 0;
  while (nwork < kRehashingWork && hmap->older.size > 0) {
    // find a non-empty slot
    HNode **from = &hmap->older.tab[hmap->migrate_pos];
    if (!*from) {
      hmap->migrate_pos++;
      continue; // empty slot
    }
    // move the first list item to the newer table
    h_insert(&hmap->newer, h_detach(&hmap->older, from));
    nwork++;
  }
  // discard the old table if done
  if (hmap->older.size == 0 && hmap->older.tab) {
    free(hmap->older.tab);
    hmap->older = HTab{};
  }
}

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
  hm_help_rehashing(hmap);
  HNode **from = h_lookup(&hmap->newer, key, eq);
  if (!from)
    from = h_lookup(&hmap->older, key, eq);
  return from ? *from : nullptr;
}

HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
  hm_help_rehashing(hmap);
  if (HNode **from = h_lookup(&hmap->newer, key, eq))
    return h_detach(&hmap->newer, from);

  if (HNode **from = h_lookup(&hmap->older, key, eq))
    return h_detach(&hmap->older, from);

  return nullptr;
}

void hm_insert(HMap *hmap, HNode *node) {
  if (!hmap->newer.tab)
    h_init(&hmap->newer, 4);

  h_insert(&hmap->newer, node);

  if (!hmap->older.tab) {
    const size_t threshold = (hmap->newer.mask + 1) * kMaxLoadFactor;
    if (hmap->newer.size >= threshold)
      hm_trigger_rehashing(hmap);
  }

  hm_help_rehashing(hmap);
}

void hm_clear(HMap *hmap) {
  free(hmap->newer.tab);
  free(hmap->older.tab);
  *hmap = HMap{};
}

size_t hm_size(HMap *hmap) { return hmap->newer.size + hmap->older.size; }

static bool h_foreach(HTab *htab, bool (*f)(HNode *, void *), void *arg) {
  if (htab->mask == 0)
    return true;

  for (size_t i = 0; i <= htab->mask; i++) {
    for (HNode *node = htab->tab[i]; node != nullptr; node = node->next) {
      if (!f(node, arg))
        return false;
    }
  }

  return true;
}

void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg) {
  h_foreach(&hmap->newer, f, arg) && h_foreach(&hmap->older, f, arg);
}
