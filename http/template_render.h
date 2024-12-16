#ifndef TEMPLATE_RENDER_H
#define TEMPLATE_RENDER_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <regex>
#include "model.h"


using namespace std;

struct Context {
    std::unordered_map<std::string, bool> boolVars;
    std::unordered_map<std::string, std::string> variables;
    std::unordered_map<std::string, std::vector<std::shared_ptr<Model>>> lists;
};


std::string renderTemplate(const std::string& templateStr, Context& context);

#endif