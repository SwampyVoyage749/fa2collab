// INI 节三方合并（纯算法，不依赖 FA2，可单测）
//
// 目标：两个人同时改同一个 INI 节时，按"键"合并，而不是整节后写覆盖。
// 三方 = base（双方共同的起点）+ local（本地当前）+ remote（对端发来的）
//
// 规则（保守）：
//   1. 只有一方改动的键 → 采用改动方
//   2. 双方都改且值相同 → 采用该值
//   3. 双方都改且值不同 → 记为冲突，默认保留 local（调用方可决定用 remote）
//   4. 一方删除（base 有、该方没有）而另一方没动 → 删除
//   5. 一方删除而另一方改了 → 记为冲突（保留改动）
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

namespace collab {

struct IniMergeResult {
    std::map<std::string, std::string> merged;
    std::vector<std::string> conflicts;   // 冲突的键名
};

inline IniMergeResult MergeIniSection(const std::map<std::string, std::string>& base,
                                      const std::map<std::string, std::string>& local,
                                      const std::map<std::string, std::string>& remote,
                                      bool preferRemote = false)
{
    IniMergeResult out;
    std::set<std::string> keys;
    for (auto& kv : base) keys.insert(kv.first);
    for (auto& kv : local) keys.insert(kv.first);
    for (auto& kv : remote) keys.insert(kv.first);

    auto get = [](const std::map<std::string, std::string>& m, const std::string& k,
                  bool& has, std::string& val) {
        auto it = m.find(k);
        has = it != m.end();
        if (has) val = it->second;
    };

    for (auto& k : keys) {
        bool inBase = false, inLocal = false, inRemote = false;
        std::string vBase, vLocal, vRemote;
        get(base, k, inBase, vBase);
        get(local, k, inLocal, vLocal);
        get(remote, k, inRemote, vRemote);

        bool localChanged = (inLocal != inBase) || (inLocal && inBase && vLocal != vBase);
        bool remoteChanged = (inRemote != inBase) || (inRemote && inBase && vRemote != vBase);

        if (!localChanged && !remoteChanged) {
            if (inLocal) out.merged[k] = vLocal;
            continue;
        }
        if (localChanged && !remoteChanged) {
            if (inLocal) out.merged[k] = vLocal;          // 本地改的，保留（含删除）
            continue;
        }
        if (!localChanged && remoteChanged) {
            if (inRemote) out.merged[k] = vRemote;        // 远端改的，采用
            continue;
        }
        // 双方都动过
        if (inLocal && inRemote && vLocal == vRemote) {
            out.merged[k] = vLocal;
            continue;
        }
        // 冲突：一边删一边改，或两边改成不同值
        out.conflicts.push_back(k);
        if (preferRemote && inRemote) out.merged[k] = vRemote;
        else if (inLocal) out.merged[k] = vLocal;
    }
    return out;
}

} // namespace collab
