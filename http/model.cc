#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <iostream>
#include "model.h"

// Derived class File
File::File(std::string name, std::string type, std::string size, std::string modified, std::string path, int index)
    : file_name(name), file_type(type), file_size(size), last_modified(modified), relative_file_path(path), file_index(index) {}

std::string File::get_attribute(const std::string& attribute_name) const {
    if (attribute_name == "file_name") return file_name;
    if (attribute_name == "file_type") return file_type;
    if (attribute_name == "file_size") return file_size;
    if (attribute_name == "last_modified") return last_modified;
    if (attribute_name == "relative_file_path") return relative_file_path;
    if (attribute_name == "file_index") return std::to_string(file_index);
    return "";
}

Animal::Animal(std::string n, std::string s, int a, std::string h, double w)
    : name(n), species(s), age(a), habitat(h), weight(w) {}

std::string Animal::get_attribute(const std::string& attribute_name) const {
    if (attribute_name == "name") return name;
    if (attribute_name == "species") return species;
    if (attribute_name == "age") return std::to_string(age);
    if (attribute_name == "habitat") return habitat;
    if (attribute_name == "weight") return std::to_string(weight);
    return "";
}


Email::Email(const std::string& id, const std::string& subject, const std::string& from, const std::string& to, const std::string& date, const std::string& content)
    : id(id), subject(subject), from(from), to(to), date(date), content(content) {}

std::string Email::get_attribute(const std::string& attribute_name) const {
    if (attribute_name == "id") return id;
    else if (attribute_name == "subject") return subject;
    else if (attribute_name == "from") return from;
    else if (attribute_name == "to") return to;
    else if (attribute_name == "date") return date;
    else if (attribute_name == "content") return content;
    else return ""; // Return an empty string if the attribute name is invalid
}
