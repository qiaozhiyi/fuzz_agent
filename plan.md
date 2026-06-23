1. **Add `format_double` utility**
   - Modify `include/fuzzpilot/string_util.hpp` using the following exact replacement block:
     ```
     <<<<<<< SEARCH
     std::string trim(std::string_view value);

     }  // namespace fuzzpilot
     =======
     std::string trim(std::string_view value);

     std::string format_double(double value);

     }  // namespace fuzzpilot
     >>>>>>> REPLACE
     ```
   - Implement `format_double` in `src/string_util.cpp` using the following exact replacement blocks:
     ```
     <<<<<<< SEARCH
     #include "fuzzpilot/string_util.hpp"

     #include <cctype>

     namespace fuzzpilot {
     =======
     #include "fuzzpilot/string_util.hpp"

     #include <cctype>
     #include <cstdio>

     namespace fuzzpilot {
     >>>>>>> REPLACE
     ```
     and
     ```
     <<<<<<< SEARCH
       return std::string(start, end);
     }

     }  // namespace fuzzpilot
     =======
       return std::string(start, end);
     }

     std::string format_double(double value) {
       char buf[32];
       int len = std::snprintf(buf, sizeof(buf), "%g", value);
       if (len > 0 && static_cast<std::size_t>(len) < sizeof(buf)) {
         return std::string(buf, static_cast<std::size_t>(len));
       }
       return std::to_string(value);
     }

     }  // namespace fuzzpilot
     >>>>>>> REPLACE
     ```

2. **Verify `format_double` additions**
   - Use `cat include/fuzzpilot/string_util.hpp` and `cat src/string_util.cpp` to verify the code modifications.

3. **Optimize Gateway JSON Payload Construction**
   - Replace the `std::ostringstream` usage in `src/model/gateway.cpp` with the following exact replacement block:
     ```
     <<<<<<< SEARCH
       std::ostringstream payload;
       payload << "{";
       payload << "\"model\":\"" << json_escape(model_) << "\",";
       if (disable_thinking_) {
         payload << "\"thinking\":{\"type\":\"disabled\"},";
       }
       payload << "\"messages\":[";
       payload << "{\"role\":\"system\",\"content\":\"" << json_escape(request.system_prompt) << "\"},";
       payload << "{\"role\":\"user\",\"content\":\"" << json_escape(request.user_context_json) << "\"}],";
       payload << "\"response_format\":{\"type\":\"json_object\"},";
       payload << "\"max_tokens\":" << request.max_output_tokens << ",";
       payload << "\"temperature\":" << request.temperature << ",";
       payload << "\"top_p\":" << request.top_p;
       if (request.seed != 0) {
         payload << ",\"seed\":" << request.seed;
       }
       payload << "}";
       const auto payload_str = payload.str();
     =======
       // Pre-allocate estimated capacity to avoid dynamic allocations
       std::string payload_str;
       payload_str.reserve(1024 + request.system_prompt.size() + request.user_context_json.size());

       payload_str += "{";
       payload_str += "\"model\":\"";
       payload_str += json_escape(model_);
       payload_str += "\",";

       if (disable_thinking_) {
         payload_str += "\"thinking\":{\"type\":\"disabled\"},";
       }

       payload_str += "\"messages\":[";
       payload_str += "{\"role\":\"system\",\"content\":\"";
       payload_str += json_escape(request.system_prompt);
       payload_str += "\"},";
       payload_str += "{\"role\":\"user\",\"content\":\"";
       payload_str += json_escape(request.user_context_json);
       payload_str += "\"}]";

       payload_str += ",\"response_format\":{\"type\":\"json_object\"}";
       payload_str += ",\"max_tokens\":";
       payload_str += std::to_string(request.max_output_tokens);
       payload_str += ",\"temperature\":";
       payload_str += format_double(request.temperature);
       payload_str += ",\"top_p\":";
       payload_str += format_double(request.top_p);

       if (request.seed != 0) {
         payload_str += ",\"seed\":";
         payload_str += std::to_string(request.seed);
       }
       payload_str += "}";
     >>>>>>> REPLACE
     ```

4. **Verify Gateway Optimization**
   - Use `cat src/model/gateway.cpp` to verify the refactoring was applied correctly.

5. **Log learning in journal**
   - Execute the following exact bash command to update `.jules/bolt.md`:
     ```bash
     cat << 'JOURNAL_EOF' >> .jules/bolt.md
     ## YYYY-MM-DD - Optimize String formatting and construction
     **Learning:** Using `std::ostringstream` for simple formatting and concatenation causes significant overhead from dynamic allocations in hot paths.
     **Action:** Do not use `std::ostringstream` for simple formatting. Use pre-allocated `std::string` (`reserve`) and standard concatenation. Use `std::snprintf` with a stack buffer for floats instead of `std::ostringstream` or `std::to_string` to avoid trailing zeros and dynamic allocation.
     JOURNAL_EOF
     ```

6. **Verify Journal Update**
   - Use `cat .jules/bolt.md` to verify the learning was logged.

7. **Format, build and test**
   - Run `clang-format -i include/fuzzpilot/string_util.hpp src/string_util.cpp src/model/gateway.cpp`
   - Run `cmake -S . -B build -G Ninja && cmake --build build`
   - Run `ctest --test-dir build --output-on-failure`

8. **Complete pre-commit steps to ensure proper testing, verification, review, and reflection are done.**
   - Call `pre_commit_instructions` and follow them.

9. **Create Pull Request**
   - Submit the changes to a new branch with the title:
     `⚡ Bolt: Optimize JSON payload construction and floating-point formatting`
   - PR description text:
     ```markdown
     ### 💡 What
     - Replaced `std::ostringstream` with a pre-allocated `std::string` (using `reserve()`) and direct `+=` string concatenation in `src/model/gateway.cpp` when constructing the JSON payload for the model gateway.
     - Implemented `fuzzpilot::format_double` using `std::snprintf` to format floats without dynamic allocations and trailing zeros, replacing the implicit format by `std::ostringstream`.

     ### 🎯 Why
     - Constructing strings with `std::ostringstream` can lead to significant overhead due to repeated dynamic memory allocations and virtual function calls. In hot paths like request payload construction, this overhead accumulates.
     - `std::to_string(double)` includes trailing zeros. Using a custom `format_double` avoids trailing zeros without the overhead of `std::ostringstream`.

     ### 📊 Impact
     - Reduces dynamic memory allocations during request serialization.
     - Improves CPU cache efficiency by utilizing a stack buffer for float formatting and pre-allocated contiguous memory for the payload.

     ### 🔬 Measurement
     - Verify correct compilation and test suite execution (`cmake --build build` and `ctest`).
     - No functional changes, but observe lower memory allocation rates in profiling.
     ```
