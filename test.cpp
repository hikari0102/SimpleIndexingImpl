#include "BPlusTree.h"
#include "BStarTree.h"
#include "BTree.h"
#include "csv_reader.h"
#include "dbindex.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

struct Student {
    std::string name;
    double gpa;
    char sex;
    double height_cm;
    double weight_kg;
    bool operator==(const Student& o) const {
        return name == o.name && gpa == o.gpa && sex == o.sex
            && height_cm == o.height_cm && weight_kg == o.weight_kg;
    }
};

struct Workload {
    std::vector<Student> records;
    std::vector<int> ins_keys;
    std::vector<int> ins_rids;
    std::vector<int> lookup_keys;
    std::vector<std::pair<int,int>> ranges;
    std::vector<int> erase_keys;
};


Workload build_workload_from_csv(const std::string& csv_path, std::size_t lookups, std::size_t erases, unsigned seed, int sorting_mode) {
    auto rows = read_student_csv(csv_path);
    if(rows.empty()) {
        throw std::runtime_error("CSV is empty: " + csv_path);
    }
    Workload w;
    std::mt19937 rng(seed);

    std::map<int, StudentRow> by_id;
    for(auto& r : rows) {
        by_id[r.id] = std::move(r);
    }

    const std::size_t M = by_id.size();
    w.records.reserve(M);
    std::vector<int> ids;       
    ids.reserve(M);
    for(auto& kv : by_id) {
        ids.push_back(kv.first);
        Student s;
        s.name = std::move(kv.second.name);
        s.gpa = kv.second.gpa;
        s.sex = (!kv.second.gender.empty() && (kv.second.gender[0] == 'M' || kv.second.gender[0] == 'm')) ? 'M' : 'F';
        s.height_cm = kv.second.height;
        s.weight_kg = kv.second.weight;
        w.records.push_back(std::move(s));
    }

    std::vector<int> perm(M);
    std::iota(perm.begin(), perm.end(), 0);
    if(sorting_mode == 0) {
        std::shuffle(perm.begin(), perm.end(), rng);
    } 
    else if(sorting_mode == 1) {
        std::sort(perm.begin(), perm.end(), [&](int a, int b) {
            return ids[a] < ids[b];
        });
    } 
    else if(sorting_mode == -1) {
        std::sort(perm.begin(), perm.end(), [&](int a, int b) {
            return ids[a] > ids[b];
        });
    }
    w.ins_keys.reserve(M);
    w.ins_rids.reserve(M);
    for(int p : perm) {
        w.ins_keys.push_back(ids[p]);
        w.ins_rids.push_back(p);
    }

    w.lookup_keys.reserve(lookups);
    std::uniform_int_distribution<int> idx_d(0, (int)ids.size() - 1);
    int lo_id = *std::min_element(ids.begin(), ids.end());
    int hi_id = *std::max_element(ids.begin(), ids.end());
    std::uniform_int_distribution<int> rand_d(lo_id, hi_id);
    for(std::size_t i = 0; i < lookups; i++) {
        if(i % 2 == 1) {
            w.lookup_keys.push_back(ids[idx_d(rng)]);
        } 
        else {
            w.lookup_keys.push_back(rand_d(rng));
        }     
    }

    auto add_range_of_size = [&](int approx_count) {
        std::vector<int> sorted = ids;
        std::sort(sorted.begin(), sorted.end());
        int max_start = std::max(0, (int)sorted.size() - approx_count - 1);
        int start = (int)(rng() % (std::size_t)(max_start + 1));
        int end = std::min((int)sorted.size() - 1, start + approx_count);
        w.ranges.emplace_back(sorted[start], sorted[end]);
    };
    int N = (int)ids.size();
    for(int i = 0; i < 100; i++) {
        add_range_of_size(std::max(1, N / 1000));
    }
    for(int i = 0; i < 20; i++) {
        add_range_of_size(std::max(1, N / 100));
    }
    for(int i = 0; i < 5; i++) {
        add_range_of_size(std::max(1, N / 10));
    }

    auto erase_pool = ids;
    std::shuffle(erase_pool.begin(), erase_pool.end(), rng);
    w.erase_keys.assign(erase_pool.begin(), erase_pool.begin() + std::min(erases, erase_pool.size()));

    return w;
}

struct Result {
    std::string name;
    double ms_insert = 0.0;
    double ms_lookup = 0.0;
    long long lookup_hits = 0;
    double ms_range_all = 0.0;
    long long range_total  = 0;
    double ms_erase = 0.0;
    bool valid_after_insert = false;
    bool valid_after_erase  = false;
    int erase_check_failures = 0;
    std::size_t size_after_insert = 0;
    std::size_t size_after_erase = 0;
    double fill_after_insert = 0.0;
    double fill_after_erase = 0.0;
    int height_after_insert = 0;
    int height_after_erase = 0;
};

template <typename Clock = std::chrono::steady_clock>
double ms_since(typename Clock::time_point t0) {
    auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

Result run_bench(dbindex<int, int>& tree, const Workload& w, bool do_periodic_check) {
    Result r;
    auto t0 = std::chrono::steady_clock::now();
    for(std::size_t i = 0; i < w.ins_keys.size(); i++) {
        tree.insert(w.ins_keys[i], w.ins_rids[i]);
    }
    r.ms_insert = ms_since(t0);
    r.size_after_insert = tree.size();
    r.valid_after_insert = tree.check();
    r.height_after_insert = tree.height();
    r.fill_after_insert = tree.fill_factor();
    t0 = std::chrono::steady_clock::now();
    for(int k : w.lookup_keys) {
        const int* rid = tree.find(k);
        if(rid) {
            const Student& s = w.records[*rid];
            r.lookup_hits += (s.name.empty() ? 0 : 1);
        }
    }
    r.ms_lookup = ms_since(t0);
    t0 = std::chrono::steady_clock::now();
    for(auto [lo, hi] : w.ranges) {
        tree.range(lo, hi, [&](const int&, const int& rid) {
            const Student& s = w.records[rid];
            r.range_total += (long long)(s.gpa * 1000);
        });
    }
    r.ms_range_all = ms_since(t0);
    if(do_periodic_check) {
        const std::size_t check_every = std::max<std::size_t>(1, w.erase_keys.size() / 10);
        double timed_ms = 0.0;
        for(std::size_t i = 0; i < w.erase_keys.size(); ++i) {
            t0 = std::chrono::steady_clock::now();
            tree.erase(w.erase_keys[i]);
            timed_ms += ms_since(t0);
            if((i + 1) % check_every == 0) {
                if(!tree.check()) {
                    r.erase_check_failures++;
                }
            }
        }
        r.ms_erase = timed_ms;
    } 
    else {
        t0 = std::chrono::steady_clock::now();
        for (int k : w.erase_keys) tree.erase(k);
        r.ms_erase = ms_since(t0);
    }
    r.size_after_erase = tree.size();
    r.valid_after_erase = tree.check();
    r.fill_after_erase = tree.fill_factor();
    r.height_after_erase = tree.height();
    return r;
}

struct IntegrityReport {
    std::string name;
    bool structural_ok = false;
    int survivors_checked = 0;
    int survivors_missing = 0;
    int survivors_wrong_val = 0;
    int deleted_checked = 0;
    int deleted_still_found = 0; 
    int range_checks = 0;
    int range_mismatches = 0;
    bool pass() const {
        return structural_ok && survivors_missing == 0 && survivors_wrong_val == 0 && deleted_still_found == 0 && range_mismatches == 0;
    }
};

IntegrityReport check_integrity(const dbindex<int, int>& tree, const Workload& w) {
    IntegrityReport r;
    r.structural_ok = tree.check();

    std::map<int, int> ref;
    for (std::size_t i = 0; i < w.ins_keys.size(); i++) {
        ref[w.ins_keys[i]] = w.ins_rids[i];
    }
    for(int k : w.erase_keys) {
        ref.erase(k);
    }
    if(ref.size() != tree.size()) {
        r.survivors_missing = (int)std::abs((long long)ref.size() - (long long)tree.size());
    }
    for(auto& [k, expected_rid] : ref) {
        r.survivors_checked++;
        const int* got_rid = tree.find(k);
        if(!got_rid) {
            r.survivors_missing++;
        } 
        else if(*got_rid != expected_rid) {
            r.survivors_wrong_val++; 
        }
    }
    for (int k : w.erase_keys) {
        if(ref.count(k)) {
            continue;
        }
        r.deleted_checked++;
        if(tree.find(k)) {
            r.deleted_still_found++;
        }
    }
    for(auto [lo, hi] : w.ranges) {
        r.range_checks++;
        std::vector<std::pair<int, int>> got;
        tree.range(lo, hi, [&](const int& k, const int& rid) {
            got.emplace_back(k, rid);
        });
        std::vector<std::pair<int, int>> expected;
        for(auto it = ref.lower_bound(lo); it != ref.end() && it->first <= hi; it++) {
            expected.emplace_back(it->first, it->second);
        }
        if(got.size() != expected.size()) { 
            r.range_mismatches++; 
            continue; 
        }
        for(std::size_t i = 0; i < got.size(); ++i) {
            if(got[i] != expected[i]) {
                r.range_mismatches++;
                break;
            }
        }
    }
    return r;
}

struct Stats {
    std::string name;
    int reps = 0;
    std::vector<double> ins, look, rng, ers;
    int erase_check_failures = 0;
    bool valid_after_insert = false;
    bool valid_after_erase = false;
    std::size_t size_after_insert = 0;
    std::size_t size_after_erase  = 0;
    long long lookup_hits = 0;
    long long range_total = 0;
    double insert_med = 0, insert_sd = 0;
    double lookup_med = 0, lookup_sd = 0;
    double range_med = 0, range_sd = 0;
    double erase_med = 0, erase_sd = 0;
    double fill_after_insert = 0.0;
    double fill_after_erase = 0.0;
    int height_after_insert = 0;
    int height_after_erase = 0;
};

static double median_of(std::vector<double> v) {
    if(v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    std::size_t n = v.size();
    return(n % 2 == 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
static double stddev_of(const std::vector<double>& v) {
    if(v.size() < 2) {
        return 0.0;
    }
    double mean = 0.0;
    for(double x : v) {
        mean += x;
    }
    mean /= (double)v.size();
    double sq = 0.0;
    for(double x : v) {
        sq += (x - mean) * (x - mean);
    }
    return std::sqrt(sq / (double)(v.size() - 1));
}

template <typename TreeFactory>
Stats run_reps(const std::string& name, TreeFactory make_tree, const Workload& w, int warmup_reps, int measured_reps) {
    Stats s;
    s.name = name;
    s.reps = measured_reps;
    for(int i = 0; i < warmup_reps; ++i) {
        auto t = make_tree();
        (void)run_bench(*t, w, false);
    }
    s.ins.reserve(measured_reps);  
    s.look.reserve(measured_reps);
    s.rng.reserve(measured_reps);  
    s.ers.reserve(measured_reps);
    for(int i = 0; i < measured_reps; i++) {
        auto t = make_tree();
        Result r = run_bench(*t, w, (i == 0));
        s.ins.push_back(r.ms_insert);
        s.look.push_back(r.ms_lookup);
        s.rng.push_back(r.ms_range_all);
        s.ers.push_back(r.ms_erase);
        if(i == 0) {
            s.erase_check_failures = r.erase_check_failures;
            s.valid_after_insert  = r.valid_after_insert;
            s.valid_after_erase = r.valid_after_erase;
            s.size_after_insert = r.size_after_insert;
            s.size_after_erase = r.size_after_erase;
            s.lookup_hits = r.lookup_hits;
            s.range_total = r.range_total;
            s.height_after_insert = r.height_after_insert;
            s.height_after_erase = r.height_after_erase;
            s.fill_after_insert = r.fill_after_insert;
            s.fill_after_erase = r.fill_after_erase;
        }
    }
    s.insert_med = median_of(s.ins); 
    s.insert_sd = stddev_of(s.ins);
    s.lookup_med = median_of(s.look);
    s.lookup_sd = stddev_of(s.look);
    s.range_med = median_of(s.rng); 
    s.range_sd = stddev_of(s.rng);
    s.erase_med = median_of(s.ers);
    s.erase_sd = stddev_of(s.ers);
    return s;
}

int main(int argc, char** argv) {
    int reps = 1;
    int warmup = -1;
    int sorting_mode = 0;
    std::string csv_out_path;
    std::vector<std::string> pos;
    for(int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if(a == "--reps" && i + 1 < argc) {
            reps = std::atoi(argv[++i]);
        } 
        else if(a == "--warmup" && i + 1 < argc) {
            warmup = std::atoi(argv[++i]);
        }
        else if(a == "--sorting" && i + 1 < argc) {
            sorting_mode = std::atoi(argv[++i]);
        } 
        else if(a == "--csv-out" && i + 1 < argc) {
            csv_out_path = argv[++i];
        }
        else {
            pos.push_back(a);
        }
    }
    if(warmup < 0) {
        warmup = (reps > 1) ? 1 : 0;
    }
    if(reps < 1) {
        reps = 1;
    }

    int order = !pos.empty() ? std::atoi(pos[0].c_str()) : 64;

    Workload w;
    if(pos.size() < 2) {
        throw std::invalid_argument("please give a csv File");
    }
    if(pos.size() >= 2) {
        const std::string& second = pos[1];
        bool is_csv = second.size() >= 4 && second.substr(second.size() - 4) == ".csv";
        if(!is_csv) {
            throw std::invalid_argument("please give a csv File");
        }
        auto Lk = (pos.size() >= 3) ? (std::size_t)std::atoll(pos[2].c_str()) : 100000;
        auto Er = (pos.size() >= 4) ? (std::size_t)std::atoll(pos[3].c_str()) : 50000;
        auto seed = (pos.size() >= 5) ? (std::size_t)std::atoll(pos[4].c_str()) : 42;
        std::cout << "Loading CSV: " << second << "\n";
        w = build_workload_from_csv(second, Lk, Er, seed, sorting_mode);
        std::cout << "  N=" << w.ins_keys.size()
                    << "  lookups=" << Lk << "  erases=" << w.erase_keys.size()
                    << "  order=" << order
                    << " (effective t=" << (order + 1) / 2 << ")\n";
    }
    std::cout << "Reps: " << reps << " measured + " << warmup
              << " warmup\n\n";

    auto make_btree = [&]{ return std::make_unique<BTree<int,int>>(order); };
    auto make_bplustree = [&]{ return std::make_unique<BPlusTree<int,int>>(order); };
    auto make_bstartree = [&]{ return std::make_unique<BStarTree<int,int>>(order); };
    std::vector<IntegrityReport> integrity;
    auto run_integrity = [&](const std::string& name, auto factory) {
        auto t = factory();
        for(std::size_t i = 0; i < w.ins_keys.size(); i++) {
            t->insert(w.ins_keys[i], w.ins_rids[i]);
        }
        for(int k : w.erase_keys) {
            t->erase(k);
        }
        IntegrityReport ir = check_integrity(*t, w);
        ir.name = name;
        integrity.push_back(std::move(ir));
    };

    std::cout << "Measuring (reps=" << reps << ", warmup=" << warmup << ")\n";
    std::vector<Stats> stats;
    auto sweep_one = [&](const std::string& name, auto factory) {
        std::cout << "  " << name << "..." << std::flush;
        stats.push_back(run_reps(name, factory, w, warmup, reps));
        std::cout << " done.\n";
        run_integrity(name, factory);
    };
    sweep_one("B-tree",  make_btree);
    sweep_one("B+-tree", make_bplustree);
    sweep_one("B*-tree", make_bstartree);
    std::cout << "\n";

    auto raw_cell_print = [&](double time) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << time;
        std::cout << std::right << std::setw(18) << oss.str();
    };

    std::cout << "============= TIMING RAW DATA =============\n";
    std::cout << std::left << std::setw(10) << "tree"
              << std::left << std::setw(10) << "rep"
              << std::right
              << std::setw(18) << "insert ms"
              << std::setw(18) << "lookup ms"
              << std::setw(18) << "range ms"
              << std::setw(18) << "erase ms"
              << "\n";

    for(auto& s : stats) {
        for(int i = 1; i <= reps; i++) {
            std::cout << std::left << std::setw(10) << s.name;
            std::cout << std::left << std::setw(10) << i;
            raw_cell_print(s.ins[i - 1]);
            raw_cell_print(s.look[i - 1]);
            raw_cell_print(s.rng[i - 1]);
            raw_cell_print(s.ers[i - 1]);
            std::cout << "\n";
        }
    }

    if(!csv_out_path.empty()) {
        std::ofstream fout(csv_out_path);
        if(!fout) {
            std::cerr << "WARNING: could not open " << csv_out_path
                      << " for writing\n";
        } 
        else {
            fout << "tree,rep,insert_ms,lookup_ms,range_ms,erase_ms\n";
            for(auto& s : stats) {
                for(int i = 0; i < reps; ++i) {
                    fout << s.name << "," << (i + 1) << ","
                         << std::fixed << std::setprecision(3)
                         << s.ins[i]  << "," << s.look[i] << ","
                         << s.rng[i]  << "," << s.ers[i]  << "\n";
                }
            }
            std::cout << "Raw measurements written to " << csv_out_path << "\n";
        }
    }

    const bool show_sd = (reps > 1);
    std::cout << "============= TIMING";
    if(show_sd) {
        std::cout << " (median \u00b1 sd over " << reps << " reps)";
    }
    std::cout << " =============\n";

    auto print_cell = [&](double med, double sd) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << med;
        if(show_sd) {
            oss << " \u00b1 " << std::setprecision(2) << sd;
        }
        std::cout << std::right << std::setw(show_sd ? 18 : 12) << oss.str();
    };

    const int col_w = show_sd ? 18 : 12;
    std::cout << std::left << std::setw(10) << "tree"
              << std::right
              << std::setw(col_w) << "insert ms"
              << std::setw(col_w) << "lookup ms"
              << std::setw(col_w) << "range ms"
              << std::setw(col_w) << "erase ms"
              << std::setw(18) << "height"
              << std::setw(18) << "fill"
              << "\n";
    std::cout << std::string(10 + 4 * col_w, '-') << "\n";
    for(auto& s : stats) {
        std::cout << std::left << std::setw(10) << s.name;
        print_cell(s.insert_med, s.insert_sd);
        print_cell(s.lookup_med, s.lookup_sd);
        print_cell(s.range_med, s.range_sd);
        print_cell(s.erase_med, s.erase_sd);
        std::cout << std::right << std::setw(18) << s.height_after_insert << "/" << s.height_after_erase;
        std::cout << std::right << std::setw(18) << std::setprecision(3) << s.fill_after_insert << "/" << s.fill_after_erase;
        std::cout << "\n";
    }
    std::cout << "\n";

    std::cout << "============= INTEGRITY =============\n";
    std::cout << std::left << std::setw(10) << "tree"
              << std::right
              << std::setw(8)  << "valid"
              << std::setw(11) << "periodic"
              << std::setw(11) << "survivors"
              << std::setw(11) << "deleted"
              << std::setw(11) << "ranges"
              << "  verdict\n";
    std::cout << std::string(78, '-') << "\n";
    for(std::size_t i = 0; i < integrity.size(); i++) {
        const auto& ir = integrity[i];
        const auto& s  = stats[i];
        std::cout << std::left << std::setw(10) << ir.name
                  << std::right
                  << std::setw(8)  << (ir.structural_ok ? "yes" : "NO!")
                  << std::setw(11) << s.erase_check_failures
                  << "  " << ir.survivors_checked
                  << "/"  << ir.survivors_missing
                  << "/"  << ir.survivors_wrong_val
                  << "  " << ir.deleted_checked
                  << "/"  << ir.deleted_still_found
                  << "  " << ir.range_checks
                  << "/"  << ir.range_mismatches
                  << "   " << (ir.pass() ? "PASS" : "FAIL")
                  << "\n";
    }
    std::cout << "\nLegend: survivors = (checked / missing / wrong-value); "
              << "deleted = (checked / still-found); ranges = (run / mismatched)\n";
    std::cout << "periodic = number of check() failures during the erase phase\n";

    bool xcheck = true;
    for(std::size_t i = 1; i < stats.size(); i++) {
        if(stats[i].lookup_hits != stats[0].lookup_hits || stats[i].range_total != stats[0].range_total || stats[i].size_after_insert != stats[0].size_after_insert || stats[i].size_after_erase != stats[0].size_after_erase) {
            xcheck = false;
        }        
    }
    std::cout << "\nCross-check (all trees agree): "<< (xcheck ? "OK" : "MISMATCH!") << "\n";
}