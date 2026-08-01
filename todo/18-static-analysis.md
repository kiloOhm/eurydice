Status: done

# Static analysis

- [x] .clang-tidy tuned for JUCE codebase
- [x] scripts/static-analysis.sh (clang-tidy + cppcheck over compile_commands.json)
- [x] Fix meaningful findings
- [x] sonar-project.properties (note: C++ needs commercial SonarQube; CE covers mcp/ JS)

Note: SonarQube CE cannot analyse C++ (CFamily is commercial). clang-tidy + cppcheck are the gate; sonar-project.properties documents all three paths.

Note: SonarQube CE cannot analyse C++ (CFamily is commercial). clang-tidy + cppcheck are the gate; sonar-project.properties documents all three paths.
