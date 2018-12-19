#include <iostream>
#include <string>
#include <vector>
#include "json.hpp"
#include <experimental/filesystem>
#include <fstream>

using namespace std;
using json = nlohmann::json;
namespace fs = std::experimental::filesystem;

enum class DependencyOrder {
    After,
    Before
};

class ModuleDependency {
public:
    string id;
    string version;
    bool optional;
    DependencyOrder order;
};

class Module {
public:
    string id;
    string file;
    string version;
    std::vector<ModuleDependency> dependencies;
};

class ModuleNode {
public:
    string id;
    Module *module;
    vector<ModuleNode *> dependencies;
};

Module *parseModule(fs::path path) {
    // Get file name
    string fileName;
    fileName = path.stem().string();

    // Parse json
    ifstream fileStream(path);
    json jsonFile;
    fileStream >> jsonFile;

    // Validate definition version
    if (jsonFile["definitionVersion"] != 1) {
        std::cout << fileName << " is in an incompatible format/version" << std::endl;
        exit(2);
    }

    // Create module
    Module *module = new Module();
    module->id = jsonFile["id"];
    module->version = jsonFile["version"];
    module->file = fileName;

    // Parse dependencies
    if (jsonFile.find("dependencies") != jsonFile.end()) {
        json dependenciesJson = jsonFile["dependencies"];

        // Loop through items
        for (json::iterator it = dependenciesJson.begin(); it != dependenciesJson.end(); ++it) {
            json entry = it.value();

            // Create dependency
            ModuleDependency dependency;
            dependency.id = it.key();
            dependency.version = entry["version"];
            dependency.order = DependencyOrder::After;

            // Parse order
            if (entry.find("order") != entry.end()) {
                if (entry["order"] == "before") {
                    dependency.order = DependencyOrder::Before;
                }
            }

            // Parse optional
            if (entry.find("optional") != entry.end()) {
                dependency.optional = entry["optional"];
            }

            module->dependencies.push_back(dependency);
        }
    }

    return module;
}

vector<string> split(string str, char separator) {
    vector<string> strings;
    istringstream stream(str);
    string s;
    while (getline(stream, s, separator)) {
        strings.push_back(s);
    }
    return strings;
}

bool areCompatible(string versionId, string version, string targetId, string target) {
    // Parse actual version
    vector<string> versionParts = split(version, '-');
    vector<string> versionSegments = split(versionParts[0], '.');
    string versionRelease = versionParts.size() > 1 ? versionParts[1] : "final";

    // Parse target
    vector<string> targetOptions = split(target, '|');

    for (vector<string>::iterator it = targetOptions.begin(); it != targetOptions.end(); ++it) {
        string targetOption = *it;

        // Parse target option
        vector<string> targetParts = split(targetOption, '-');
        vector<string> targetSegments = split(targetParts[0], '.');
        string targetRelease = targetParts.size() > 1 ? targetParts[1] : "final";

        int segmentCount = min(versionSegments.size(), targetSegments.size());

        for (int i = 0; i < segmentCount; i++) {
            string versionSegment = versionSegments[i];
            string targetSegment = targetSegments[i];

            // Ensure not empty
            if (versionSegment.size() < 1) {
                cout << "Invalid version " << version << " (from " << versionId << ")" << endl;
                exit(4);
            }

            // Ensure not empty
            if (targetSegment.size() < 1) {
                cout << "Invalid version target " << target << " (from " << targetId << " for " << versionId << ")"
                     << endl;
                exit(4);
            }

            int value = stoi(versionSegment);

            // Check version target type
            if (targetSegment[0] == '[') {
                // Ensure format
                if (targetSegment[targetSegment.size() - 1] != ']' || targetSegment.size() < 3) {
                    cout << "Invalid version target " << target << " (from " << targetId << " for " << versionId << ")"
                         << endl;
                    exit(4);
                }

                // Parse
                vector<string> rangeSegments = split(targetSegment.substr(1, targetSegment.size() - 2), ',');

                // Ensure two values
                if (rangeSegments.size() != 2) {
                    cout << "Invalid version target " << target << " (from " << targetId << " for " << versionId << ")"
                         << endl;
                    exit(4);
                }

                // Parse min max
                int min = stoi(rangeSegments[0]);
                int max = stoi(rangeSegments[1]);

                // Ensure within min max
                if (!(value >= min && value <= max)) {
                    goto outerEnd;
                }
            } else if (targetSegment[targetSegment.size() - 1] == '+') {
                // Validate format
                if (targetSegment.size() < 2) {
                    cout << "Invalid version target " << target << " (from " << targetId << " for " << versionId << ")"
                         << endl;
                    exit(4);
                }

                // Parse
                int min = stoi(targetSegment.substr(0, targetSegment.size() - 1));

                // Ensure within bound
                if (value < min) {
                    goto outerEnd;
                }
            } else if (targetSegment[targetSegment.size() - 1] == '-') {
                // Validate format
                if (targetSegment.size() < 2) {
                    cout << "Invalid version target " << target << " (from " << targetId << " for " << versionId << ")"
                         << endl;
                    exit(4);
                }

                // Parse
                int max = stoi(targetSegment.substr(0, targetSegment.size() - 1));

                // Ensure within bound
                if (value > max) {
                    goto outerEnd;
                }
            } else {
                // Parse
                int targetValue = stoi(targetSegment);

                // Ensure equals target
                if (value != targetValue) {
                    goto outerEnd;
                }
            }
        }

        // If target requests a more fine version, then assume incorrect
        if (targetSegments.size() > versionSegments.size()) {
            goto outerEnd;
        }

        // If target release is later, then incorrect
        if (targetRelease.compare(versionRelease) > 1) {
            goto outerEnd;
        }

        // If others pass, then must be compatible
        return true;
        outerEnd:;
    }

    return false;
}

void resolve(ModuleNode *node, vector<ModuleNode *> *resolved, vector<ModuleNode *> *unresolved) {
    // Mark node as unresolved
    unresolved->push_back(node);

    // Resolve dependencies
    for (vector<ModuleNode *>::iterator it = node->dependencies.begin(); it != node->dependencies.end(); ++it) {
        ModuleNode *dependency = *it;

        // If node hasn't been resolved, resolve it
        if (find(resolved->begin(), resolved->end(), dependency) == resolved->end()) {
            // Check for circular dependencies
            if (find(unresolved->begin(), unresolved->end(), dependency) != unresolved->end()) {
                cout << "Circular dependency detected! " << node->id << " to " << dependency->id << endl;
                exit(6);
            }

            // Resolve dependency
            resolve(dependency, resolved, unresolved);
        }
    }

    // Mark as resolved
    resolved->push_back(node);
    unresolved->erase(remove(unresolved->begin(), unresolved->end(), node), unresolved->end());
}

int main(int argc, char *argv[]) {
    // Parse arguments
    if (argc != 2) {
        cout << "Invalid argument count" << endl;
        exit(1);
    }

    string rootPath = argv[1];

    // Parse all module json files
    vector<Module *> modules;
    for (const auto &entry : fs::directory_iterator(rootPath)) {
        fs::path path = entry.path();

        // Validate file
        if (!fs::is_directory(path) && path.has_extension() && path.extension() == ".json") {
            // Parse
            Module *module = parseModule(path);
            modules.push_back(module);
        }
    }

    // Generate nodes for all modules
    map<string, ModuleNode *> nodes;
    for (vector<Module *>::iterator it = modules.begin(); it != modules.end(); ++it) {
        Module *module = *it;

        // Create node
        ModuleNode *node = new ModuleNode();
        node->id = module->id;
        node->module = module;

        // Store
        nodes.insert(make_pair(node->id, node));
    }

    // Generate dependency links
    for (map<string, ModuleNode *>::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        ModuleNode *node = it->second;
        Module *module = node->module;

        vector<ModuleDependency> *dependencies = &module->dependencies;
        for (vector<ModuleDependency>::iterator it2 = dependencies->begin(); it2 != dependencies->end(); ++it2) {
            ModuleDependency dependency = *it2;

            // Check whether dependency exists
            if (nodes.find(dependency.id) == nodes.end()) {
                if (dependency.optional) {
                    continue;
                }
                cout << "Required dependency " << dependency.id << " is missing for " << module->id << endl;
                exit(3);
            }

            // Fetch other node
            ModuleNode *otherNode = nodes[dependency.id];
            Module *otherModule = otherNode->module;

            // Check compatible
            if (!areCompatible(module->id, otherModule->version, dependency.id, dependency.version)) {
                cout << "Required dependency " << dependency.id << "(" << dependency.version << ") for " << module->id
                     << " is not compatible with " << otherModule->id << "(" << otherModule->version << ")" << endl;
                exit(5);
            }

            // Add dependency
            switch (dependency.order) {
                case DependencyOrder::After:
                    node->dependencies.push_back(otherNode);
                    break;
                case DependencyOrder::Before:
                    otherNode->dependencies.push_back(node);
                    break;
            }
        }
    }

    // Find unrooted nodes
    vector<ModuleNode *> unrooted;
    for (map<string, ModuleNode *>::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        ModuleNode *node = it->second;
        unrooted.push_back(node);
    }

    for (map<string, ModuleNode *>::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        ModuleNode *node = it->second;
        vector<ModuleNode *> dependencies;

        for (vector<ModuleNode *>::iterator it2 = dependencies.begin(); it2 != dependencies.end(); ++it2) {
            ModuleNode *dependency = *it2;
            unrooted.erase(remove(unrooted.begin(), unrooted.end(), dependency), unrooted.end());
        }
    }

    // Create fake node
    ModuleNode *root = new ModuleNode();
    root->id = "root";

    for (vector<ModuleNode *>::iterator it = unrooted.begin(); it != unrooted.end(); ++it) {
        ModuleNode *node = *it;
        root->dependencies.push_back(node);
    }

    // Resolve
    vector<ModuleNode *> resolved;
    vector<ModuleNode *> unresolved;
    resolve(root, &resolved, &unresolved);

    // Remove fake node
    resolved.erase(remove(resolved.begin(), resolved.end(), root), resolved.end());

    // Print
    for (vector<ModuleNode *>::iterator it = resolved.begin(); it != resolved.end(); ++it) {
        ModuleNode *node = *it;
        Module *module = node->module;

        cout << module->id << "#" << module->file << endl;
    }

    return 0;
}
