#include "template_render.h"

std::string renderTemplate(const std::string& templateStr, Context& context) {
    std::string output;
    std::istringstream stream(templateStr);
    std::string line;

    std::regex forLoopRegex(R"(<%\s*for\s*\(\s*(\w+)\s*:\s*(\w+)\s*\)\s*%>)");
    std::regex endForRegex(R"(<%\s*endfor\s*%>)");
    std::regex ifRegex(R"(<%\s*if\s*\(\s*(\w+)\s*\)\s*%>)");      // Matches <% if (condition) %>
    std::regex elseRegex(R"(<%\s*else\s*%>)");                     // Matches <% else %>
    std::regex endIfRegex(R"(<%\s*endif\s*%>)");                   // Matches <% endif %>
    std::regex jsMarkerRegex(R"(<%.*%>)");               

    std::vector<bool> ifStack; // Stack to track active `if` blocks
    bool skipBlock = false;    // Flag to determine if current block should be skipped

    std::string loopVar, listName, loopBody;
    bool inForLoop = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::smatch match;

        // Handle `if` condition
       
            if (std::regex_search(line, match, ifRegex)) {
                std::string condition = match[1];
                bool conditionValue = context.boolVars.count(condition) ? context.boolVars[condition] : false;
                ifStack.push_back(conditionValue);

                // Update `skipBlock` based on condition and any previous `if` blocks
                skipBlock = skipBlock || !conditionValue;
            }

            // Handle `else` statement
            else if (std::regex_search(line, match, elseRegex)) {
                if (!ifStack.empty()) {
                    bool previousCondition = ifStack.back();
                    ifStack.back() = !previousCondition;  // Flip the last condition
                    skipBlock = !ifStack.back() || (ifStack.size() > 1 && skipBlock);
                }
            }

            // Handle `endif` statement
            else if (std::regex_search(line, match, endIfRegex)) {
                if (!ifStack.empty()) {
                    ifStack.pop_back();
                    skipBlock = !ifStack.empty() && !ifStack.back();  // Update `skipBlock` based on remaining stack
                }
            }

            // Skip line if inside an unsatisfied `if` block
            if (skipBlock) {
                continue;
            }

            // Handle `for` loop only if `skipBlock` is false
            if (std::regex_search(line, match, forLoopRegex)) {
                inForLoop = true;
                loopVar = match[1];
                listName = match[2];
                loopBody.clear();
            } else if (std::regex_search(line, match, endForRegex)) {
                inForLoop = false;
                if (context.lists.count(listName)) {
                    const auto& list = context.lists[listName];
                    for (const auto& item : list) {
                        std::string renderedBody = loopBody;
                        std::regex varRegex(R"(<%=\s*)" + loopVar + R"(\.(\w+)\s*%>)");
  // Match attribute pattern
                        std::smatch attributeMatch;
                        while (std::regex_search(renderedBody, attributeMatch, varRegex)) {
                            std::string attributeName = attributeMatch[1];
                            std::string value = item->get_attribute(attributeName);
                            renderedBody.replace(attributeMatch.position(0), attributeMatch.length(0), value);
                        }
                        output += renderedBody + "\n";
                    }
                }
            } else if (inForLoop) {
                // Collect lines inside the `for` loop body
                loopBody += line + "\n";
            }
        
        // Process variable substitution for lines outside of loops and conditionals
        else {
            // if (std::regex_search(line, jsMarkerRegex)) continue;
            std::regex varRegex(R"(<%=\s*(\w+)\s*%>)");
            bool modified = false;
            while (std::regex_search(line, match, varRegex)) {
                std::string varName = match[1];
                std::string value = context.variables.count(varName) ? context.variables[varName] : "";
                line.replace(match.position(0), match.length(0), value);
                modified = true;
            }
            if (std::regex_search(line, jsMarkerRegex) and !modified) continue;
            output += line + "\n";
        }
    }

    return output;
}
