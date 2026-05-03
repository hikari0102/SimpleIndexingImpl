// csv_reader.h -- minimal CSV reader for the student dataset.
//
// Assumes the simple format used by the assignment data:
//   - Comma-separated, no quoted fields, no embedded commas
//   - First row is a header
//   - Columns: Student ID, Name, Gender, GPA, Height, Weight
//
// Blank lines are skipped silently (the assignment data has them between rows).
#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct StudentRow {
    int id;
    std::string name;
    std::string gender;
    double gpa;
    double height;
    double weight;
};

inline std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\r' || s[b] == '\n')) {
        b++;
    }
    while (e > b && (s[e-1] == ' ' || s[e-1] == '\r' || s[e-1] == '\n')) {
        e--;
    }
    return s.substr(b, e - b);
}

inline std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string field;
    while(std::getline(ss, field, ',')) {
        out.push_back(trim(field));
    }
    return out;
}

inline std::vector<StudentRow> read_student_csv(const std::string& path) {
    std::ifstream in(path);
    if(!in) {
        throw std::runtime_error("Cannot open CSV: " + path);
    }

    std::vector<StudentRow> rows;
    std::string line;
    bool header_flag = false;
    std::size_t line_no = 0;

    while(std::getline(in, line)) {
        line_no++;
        std::string t = trim(line);
        if(t.empty()) {
            continue;
        }

        if(!header_flag) {
            header_flag = true;
            try {
                std::stoi(split_csv_line(t).at(0));
            } 
            catch(...) {
                continue; 
            }
        }

        auto fields = split_csv_line(t);
        if(fields.size() < 6) {
            throw std::runtime_error( "Line " + std::to_string(line_no) + ": expected 6 columns, got " + std::to_string(fields.size()));
        }
        try {
            StudentRow r;
            r.id = std::stoi(fields[0]);
            r.name = fields[1];
            r.gender = fields[2];
            r.gpa = std::stod (fields[3]);
            r.height = std::stod (fields[4]);
            r.weight = std::stod (fields[5]);
            rows.push_back(std::move(r));
        } 
        catch(const std::exception& ex) {
            throw std::runtime_error("Line " + std::to_string(line_no) + ": parse error (" + ex.what() + ")");
        }
    }
    return rows;
}
