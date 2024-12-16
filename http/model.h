// Model.h
#ifndef MODEL_H
#define MODEL_H

#include <string>

class Model {
public:
    virtual ~Model() = default;
    virtual std::string get_attribute(const std::string& attribute_name) const = 0;
};


class File : public Model {
public:
    std::string file_name;
    std::string file_type;
    std::string file_size;
    std::string last_modified;
    std::string relative_file_path;
    int file_index;

    File(std::string name, std::string type, std::string size, std::string modified, std::string path, int index);
    std::string get_attribute(const std::string& attribute_name) const override;
};

class Animal : public Model {
public:
    std::string name;
    std::string species;
    int age;
    std::string habitat;
    double weight;

    Animal(std::string n, std::string s, int a, std::string h, double w);
    std::string get_attribute(const std::string& attribute_name) const override;
};

class Email : public Model {
public:
    std::string id;
    std::string subject;
    std::string from;
    std::string to;
    std::string date;
    std::string content;

    Email(const std::string& id, const std::string& subject, const std::string& from, const std::string& to, const std::string& date, const std::string& content);
    std::string get_attribute(const std::string& attribute_name) const override;
};

#endif // MODEL_H
