#ifndef SCHEDULES_H_
#define SCHEDULES_H_

#include <atomic>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glog/logging.h>

#include "generator.h"
#include "schedule.h"

inline std::ostream& operator<<(std::ostream& os,
                                const std::vector<bool>& bits) {
  for (auto bit : bits) {
    os << (bit ? '1' : '0');
  }
  return os;
}

inline std::ostream& operator<<(std::ostream& os,
                                const std::list<size_t>& list) {
  for (auto elem : list) {
    os << elem << ", ";
  }
  return os;
}

inline std::ostream& operator<<(std::ostream& os,
                                const std::vector<Attr>& list) {
  for (auto elem : list) {
    os << elem << ", ";
  }
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const Schedule::Ptr& p) {
  return os << *p;
}

inline std::ostream& operator<<(std::ostream& os, const AAttrUnion& v) {
  std::visit([&os](auto&& arg) { os << arg; }, v);
  return os;
}

struct Schedules {
  using Bits = std::vector<bool>;
  using Key = std::vector<Attr>;
  using Cache = std::unordered_map<Key, std::shared_ptr<const Schedules>>;
  struct Stat {
    uint64_t cache_miss;
    uint64_t cache_hit;
    uint64_t trip_count[3];
  };

  const std::vector<RAttr>& rattrs;
  const std::vector<AAttr>& aattrs;
  Cache* cache;
  std::shared_ptr<Stat> stat;
  Bits operands;
  mutable std::vector<AAttrUnion> schedules;
  static bool cache_schedules;
  static bool bottom_up;

  static std::atomic_uint64_t constructed;
  static std::atomic_uint64_t deconstructed;
  void* operator new(size_t size) {
    ++constructed;
    return ::operator new(size);
  }
  void operator delete(void* ptr) {
    ++deconstructed;
    ::operator delete(ptr);
    if (constructed == deconstructed) {
      LOG(INFO) << "Schedules construct-and-deconstructed: "
                << constructed.load();
    }
  }

  Schedules(const std::vector<RAttr>& rattrs, const std::vector<AAttr>& aattrs,
            const Bits* operands = nullptr, Cache* cache = nullptr,
            const std::shared_ptr<Stat>& stat = {})
      : rattrs(rattrs),
        aattrs(aattrs),
        cache(cache),
        stat(stat),
        operands(operands != nullptr ? *operands : Bits(rattrs.size(), true)) {
    VLOG(7) << "Schedules constructed for operands " << this->operands;
    if (cache != nullptr) {
      if (cache
              ->insert({KeyOf(this->operands),
                        std::shared_ptr<const Schedules>(this)})
              .second) {
        VLOG(8) << "insert into cache";
      }
    }
    if (stat == nullptr) {
      this->stat.reset(new Stat{0, 0, {}});
    }
  }

  Key KeyOf(const Bits& operands) const;
  Generator<AAttrUnion> Generate() const;
  Generator<AAttrUnion> GetSchedules(const Bits operands) const {
    if (cache != nullptr) {
      auto schedules = cache->find(KeyOf(operands));
      if (schedules != cache->end()) {
        ++stat->cache_hit;
        return schedules->second->Generate();
      }
    }
    ++stat->cache_miss;
    return (new Schedules(rattrs, aattrs, &operands, cache, stat))->Generate();
  }
  Schedule Best() {
    auto schedules = Generate();
    Schedule::Ptr best;
    size_t num_ops = 0;
    size_t total_distance = 0;
    size_t num_schedule = 0;
    for (const auto& aattr_union : schedules) {
      auto schedule = std::get<Schedule::Ptr>(aattr_union);
      auto schedule_num_ops = schedule->NumOps();
      size_t schedule_total_distance = -1;
      VLOG(2) << "schedule: " << schedule << " num_ops: " << schedule_num_ops
              << (best != nullptr
                      ? " best: " + best->ToStrWithOffset() +
                            " num_ops: " + std::to_string(num_ops) +
                            " total_distance: " + std::to_string(total_distance)
                      : "");
      ++num_schedule;
      if (best == nullptr || schedule_num_ops < num_ops ||
          (schedule_num_ops == num_ops &&
           (schedule_total_distance = schedule->TotalDistance()) <
               total_distance)) {
        best = std::move(schedule);
        num_ops = schedule_num_ops;
        total_distance = best->TotalDistance();
      }
    }
    LOG(INFO) << num_schedule << " schedules evaluated";
    return *best;
  }
};

#endif  // SCHEDULES_H_