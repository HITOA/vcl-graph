#include <VCLG/CodeGen/Migration.hpp>

#include <llvm/ADT/StringMap.h>

#include <cstring>


uint64_t VCLG::MigrationPlan::Bytes() const {
    uint64_t bytes = 0;
    for (const Copy& copy : copies)
        bytes += copy.size;
    return bytes;
}

void VCLG::MigrationPlan::Apply(const void* from, void* to) const {
    for (const Copy& copy : copies)
        std::memcpy((uint8_t*)to + copy.to, (const uint8_t*)from + copy.from, copy.size);
}

VCLG::MigrationPlan VCLG::PlanMigration(const GraphLayout& from, const GraphLayout& to) {
    llvm::StringMap<const GraphLayout::Region*> old{};
    for (const GraphLayout::Region& region : from.regions)
        old[region.key] = &region;

    MigrationPlan plan{};
    for (const GraphLayout::Region& region : to.regions) {
        auto it = old.find(region.key);
        if (it == old.end())
            continue;
        const GraphLayout::Region& previous = *it->second;
        // The signature covers the layout; the kind and size are checked anyway, since a copy of
        // the wrong size writes outside the region.
        if (previous.signature != region.signature || previous.kind != region.kind || previous.size != region.size
                || region.size == 0)
            continue;
        if (!plan.copies.empty()) {
            MigrationPlan::Copy& last = plan.copies.back();
            if (last.from + last.size == previous.offset && last.to + last.size == region.offset) {
                last.size += region.size;
                continue;
            }
        }
        plan.copies.push_back({ previous.offset, region.offset, region.size });
    }
    return plan;
}
