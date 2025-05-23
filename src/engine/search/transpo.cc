#include "transpo.h"

#include <thread>

#include "../evaluation/evaluation.h"

namespace search {

[[nodiscard]] TranspositionTableEntry *TranspositionTable::Probe(
    const U64 &key) {
  auto &cluster = (*this)[key];
  // Default to replacing the first entry (if it's available)
  auto replace_entry = &cluster.entries[0];
  // Find another entry if the first one is already taken
  if (replace_entry->key != 0 && !replace_entry->CompareKey(key)) {
    for (int i = 1; i < kTTClusterSize; i++) {
      const auto entry = &cluster.entries[i];
      // If this entry is available, we can attempt to write to it
      if (entry->key == 0 || entry->CompareKey(key)) {
        return entry;
      }
      // Always prefer the lowest quality entry
      const int lowest_quality =
          replace_entry->depth - 8 * GetAgeDelta(replace_entry);
      const int current_quality = entry->depth - 8 * GetAgeDelta(entry);
      if (lowest_quality > current_quality) {
        replace_entry = entry;
      }
    }
  }

  return replace_entry;
}

void TranspositionTable::Save(TranspositionTableEntry *old_entry,
                              TranspositionTableEntry new_entry,
                              const U64 &key,
                              I32 ply,
                              bool in_pv) {
  if (new_entry.move || !old_entry->CompareKey(key)) {
    old_entry->move = new_entry.move;
  }

  if (!old_entry->CompareKey(key) ||
      new_entry.flag == TranspositionTableEntry::kExact ||
      new_entry.depth + 3 + 2 * in_pv >= old_entry->depth ||
      old_entry->age != age_) {
    new_entry.age = age_;

    old_entry->key = static_cast<U16>(key);
    old_entry->score =
        TranspositionTableEntry::CorrectScore(new_entry.score, -ply);
    old_entry->depth = new_entry.depth;
    old_entry->age = new_entry.age;
    old_entry->flag = new_entry.flag;
    old_entry->was_in_pv = new_entry.was_in_pv;
    old_entry->static_eval = new_entry.static_eval;
  }
}

U32 TranspositionTable::GetAgeDelta(
    const TranspositionTableEntry *entry) const {
  return (kMaxTTAge + age_ - entry->age) % kMaxTTAge;
}

void TranspositionTable::Age() {
  age_ = (age_ + 1) % kMaxTTAge;
}

int TranspositionTable::HashFull() const {
  int count = 0;
  for (int i = 0; i < 1000; i++) {
    count +=
        std::ranges::count_if(table_[i].entries, [this](const auto &entry) {
          return entry.age == age_ && entry.key != 0 &&
                 entry.score != kScoreNone;
        });
  }
  return count / kTTClusterSize;
}

void TranspositionTable::Clear(int num_threads) {
  const std::size_t chunks = (table_size_ + num_threads - 1) / num_threads;

  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([i, chunks, this]() {
      const std::size_t clear_index = chunks * i;
      const std::size_t clear_size =
          std::min(chunks, table_size_ - clear_index) *
          sizeof(TranspositionTableCluster);
      std::memset(table_ + clear_index, 0, clear_size);
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  age_ = 0;
}

}  // namespace search