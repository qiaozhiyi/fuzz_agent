#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace fuzzpilot {

// 从 agent 提案 JSON 中提取字典 token
// 支持的 JSON 路径或键：
//   - .entries[].value       (DictionaryAgent 格式)
//   - .dictionary_tokens[]   (直接列表)
//   - .interventions[].entries[].value (嵌套格式)
//   - .seed_strategies[].dictionary_tokens[]
//   - .magic_values[] / .values[]
// 对 hex 字面值（如 "0x3C21444F4354595045"）自动解码为 ASCII，去重，限制大小与非控制字符
std::vector<std::string> extract_dictionary_tokens_from_proposal(
    const std::string& proposal_json);

// 从 AFL++ 字典文件（.dict）中加载 token
std::vector<std::string> load_tokens_from_dict(
    const std::filesystem::path& dict_path);

}  // namespace fuzzpilot
