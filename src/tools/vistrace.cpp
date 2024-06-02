#ifndef LIBCORO_ENABLE_TRACE
#define LIBCORO_ENABLE_TRACE
#endif

#include "../coro/trace.h"
#include "getoptxx.h"


#include <variant>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <charconv>
#include <numeric>
#include <cstdlib>

#include <cxxabi.h>


#include <algorithm>
struct coro_info_t {
    std::string id = {};
    std::string name = {};
    std::string file = {};
    std::size_t size = 0;
    std::string user_data = {};
    std::string type = {};
    bool _destroyed = false;
    std::string generate_label(bool nl) const;

};

struct thread_state_t {

    using coro_stack_t = std::deque<std::string>;
    using main_stack_t = std::stack<coro_stack_t>;

    main_stack_t _stack;
    std::string _last_active;
    std::string _tid;
    std::string generate_label(unsigned int id) const;

};

struct rel_create {
    std::string _target;
    bool _suspended = false;
    bool operator==(const rel_create &) const = default;
};

struct rel_destroy {
    enum type {
        t_call,
        t_suspend,
        t_return
    };
    std::string _target;
    type _type = t_call;
    bool operator==(const rel_destroy &) const = default;
};

struct rel_yield {
    std::string _type;
    bool operator==(const rel_yield &) const = default;
};


struct rel_await {
    std::string _type;
    std::size_t _offset = 0;
    bool operator==(const rel_await &) const = default;
};

struct rel_resume {
    std::string _target;
    bool operator==(const rel_resume &) const = default;
};

struct rel_return {
    std::string _target;
    bool operator==(const rel_return &) const = default;
};

struct rel_suspend{
    std::string _target;
    bool operator==(const rel_suspend &) const = default;
};

struct rel_switch{
    std::string _target;
    bool operator==(const rel_switch &) const = default;
};
struct rel_user_log {
    std::string text;
    bool operator==(const rel_user_log &) const = default;
};
struct rel_unknown_switch {
    std::string _target;
    bool operator==(const rel_unknown_switch &) const = default;
};
struct rel_hline {
    std::string _text;
    bool operator==(const rel_hline &) const = default;
};
struct rel_loop {
    std::size_t count;
    bool operator==(const rel_loop &) const = default;
};

struct rel_end_loop {
    std::size_t count;
    bool operator==(const rel_end_loop &) const = default;
};


template<typename T>
concept has_target = requires(T t) {
    {t._target};
};

using relation_type_t = std::variant<std::monostate,
                                     rel_create,
                                     rel_destroy,
                                     rel_yield,
                                     rel_await,
                                     rel_resume,
                                     rel_suspend,
                                     rel_switch,
                                     rel_return,
                                     rel_user_log,
                                     rel_unknown_switch,
                                     rel_hline,
                                     rel_loop,
                                     rel_end_loop
                                     >;


struct relation_t {
    unsigned int _thread = 0;
    std::string _coro;
    relation_type_t _rel_type;
    bool operator==(const relation_t &other) const = default;
    bool equal_ignore_user(const relation_t &other) const {
        return _thread == other._thread
                && _coro == other._coro
                && std::visit([](const auto &a, const auto &b){
            using T = std::decay_t<decltype(a)>;
            using U = std::decay_t<decltype(b)>;
            if constexpr(std::is_same_v<T, U>) {
                if constexpr(std::is_same_v<T, rel_user_log>) {
                    return true;
                } else {
                    return a == b;
                }
            } else {
                return false;
            }
        }, _rel_type, other._rel_type);
    }
};


using coro_map_t = std::unordered_map<std::string, coro_info_t>;
using thread_map_t = std::unordered_map<unsigned int, thread_state_t>;
using relation_list_t = std::vector<relation_t>;

class App {
public:


    coro_map_t _coro_map;
    thread_map_t _thread_map;
    relation_list_t _relations;


    bool parse_line(std::istream &f, std::vector<std::string_view> &parts);

    void parse(std::istream &f);

    void export_uml(std::ostream &out, unsigned int label_size);

    bool filter_active();
    void filter_nevents(unsigned int n);

    template<bool ignore_user>
    void detect_loops();
protected:
    std::string b1;

    const std::string &get_active_coro(unsigned int thread);
    void deactivate_coro(unsigned int thread, std::string_view id);
    void activate_coro(unsigned int thread, std::string_view id);
    void ensure_active_coro(unsigned int thread, std::string_view id);
    void set_name(std::string_view id, std::string_view file, std::string_view name, std::string_view user);
    void set_type(std::string_view id, std::string_view type);
    void add_stack_level(unsigned int thread);
    void remove_stack_level(unsigned int thread);
    coro_map_t::iterator introduce_coro(std::string_view id);
    void create_coro(std::string_view id, std::size_t sz);
    void mark_destroyed(std::string_view id);

    static std::string demangle(const std::string &txt);
    static std::string short_label_size_template(std::string txt, unsigned int size);
    void set_thread(unsigned int thread, std::string_view id);

    void solve_conflict(std::string id);
    void filter_actors();
    template<bool ignore_user>
    bool detect_loop_cycle();
};




inline bool App::parse_line(std::istream &f, std::vector<std::string_view> &parts) {
    std::getline(f,b1);
    if (b1.empty()) return false;
    auto sep = b1.find(coro::trace::separator);
    auto psep = 0;

    parts.clear();
    while (sep != b1.npos) {
        parts.push_back(std::string_view(b1).substr(psep, sep - psep));
        psep = sep+1;
        sep = b1.find(coro::trace::separator, psep);
    }
    parts.push_back(std::string_view(b1).substr(psep));
    return true;
}

inline void App::parse(std::istream &f) {
    using type = coro::trace::record_type;
    try {
        std::vector<std::string_view> parts;
        while (parse_line(f, parts)) {

            relation_t rel;

            auto [ptr, ec] = std::from_chars(parts[0].begin(), parts[1].end(), rel._thread, 10);
            if (ec != std::errc()) {
                throw 0;
            }
            char flag;
            if (parts.at(1).size() != 1) {
                throw 1;
            }
            flag = parts.at(1)[0];

            switch (static_cast<type>(flag)) {
                case type::thread:
                    set_thread(rel._thread, parts.at(2));
                    continue;
                case type::create:
                    rel._coro = get_active_coro(rel._thread);
                    {
                        std::size_t sz;
                        auto [ptr, ec] = std::from_chars(parts[3].begin(), parts[3].end(), sz, 10);
                        if (ec != std::errc()) {
                            throw 3;
                        }
                        create_coro(parts.at(2), sz);
                    }
                    rel._rel_type = rel_create{std::string(parts.at(2))};
                    activate_coro(rel._thread, parts.at(2));
                    break;
                case type::destroy:
                    if (!_relations.empty()
                            && std::holds_alternative<rel_suspend>(_relations.back()._rel_type)
                            && _relations.back()._coro == parts.at(2)) {

                        rel._coro = _relations.back()._coro;
                        rel._rel_type = rel_destroy{
                            std::get<rel_suspend>(_relations.back()._rel_type)._target,
                            rel_destroy::t_suspend
                        };
                        _relations.pop_back();
                    } else {
                        rel._coro = get_active_coro(rel._thread);
                        std::string destroying_coro ( parts.at(2));
                        if (destroying_coro == rel._coro) {
                            deactivate_coro(rel._thread, destroying_coro);
                            rel._rel_type = rel_destroy{get_active_coro(rel._thread), rel_destroy::t_suspend};
                        } else {
                            deactivate_coro(rel._thread, destroying_coro);
                            rel._rel_type = rel_destroy{destroying_coro, rel_destroy::t_call};
                        }
                    }
                    mark_destroyed(parts.at(2));
                    break;
                case type::hr:
                    rel._rel_type = rel_hline{std::string(parts.at(2))};
                    break;
                case type::coroutine_type:
                    set_type(parts.at(2), parts.at(3));
                    continue;
                case type::name:
                    set_name(parts.at(2), parts.at(3), parts.at(4), parts.at(5));
                    continue;
                    break;
                case type::resume_enter:
                    rel._coro = get_active_coro(rel._thread);
                    rel._rel_type = rel_resume{std::string(parts.at(2))};
                    add_stack_level(rel._thread);
                    activate_coro(rel._thread,parts.at(2));
                    break;
                case type::resume_exit:
                    if (!_relations.empty()
                            && std::holds_alternative<rel_suspend>(_relations.back()._rel_type)
                            && std::get<rel_suspend>(_relations.back()._rel_type)._target.empty()) {
                        //after switch to "unknown"
                        rel._coro = _relations.back()._coro;
                        remove_stack_level(rel._thread);
                        rel._rel_type = rel_return{get_active_coro(rel._thread)};
                        _relations.pop_back();

                    } else if (!_relations.empty()
                                && std::holds_alternative<rel_destroy>(_relations.back()._rel_type)
                                && std::get<rel_destroy>(_relations.back()._rel_type)._type == rel_destroy::t_suspend
                                && get_active_coro(rel._thread).empty()) {
                         rel._coro = _relations.back()._coro;
                         remove_stack_level(rel._thread);
                         rel._rel_type = rel_destroy{get_active_coro(rel._thread), rel_destroy::t_return};
                         _relations.pop_back();
                    }else {
                        rel._coro = get_active_coro(rel._thread);
                        remove_stack_level(rel._thread);
                        rel._rel_type = rel_return{get_active_coro(rel._thread)};
                    }
                    break;
                case type::sym_switch:
                    ensure_active_coro(rel._thread, parts.at(2));
                    rel._coro = get_active_coro(rel._thread);
                    if (parts.at(3) == "0") {
                        deactivate_coro(rel._thread,rel._coro);
                        if (!_relations.empty()
                                && std::holds_alternative<rel_create>(_relations.back()._rel_type)) {
                            std::get<rel_create>(_relations.back()._rel_type)._suspended = true;
                            continue;
                        } else {
                            rel._rel_type = rel_suspend{get_active_coro(rel._thread)};
                        }
                    } else {
                        deactivate_coro(rel._thread,rel._coro);
                        activate_coro(rel._thread, parts.at(3));
                        rel._rel_type = rel_switch{get_active_coro(rel._thread)};
                    }
                    break;
                case type::user_report:
                    rel._coro = get_active_coro(rel._thread);
                    rel._rel_type = rel_user_log{
                        std::accumulate(parts.begin()+3, parts.end(), std::string(parts.at(2)),
                                [](std::string &&acc, std::string_view str){
                            acc.push_back(coro::trace::separator);
                            acc.append(str);
                            return std::move(acc);
                        })
                    };
                    break;
                case type::awaits_on:
                    ensure_active_coro(rel._thread, parts.at(2));
                    rel._coro = get_active_coro(rel._thread);
                    rel._rel_type = rel_await{demangle(std::string(parts.at(3)))};
                    break;
                case type::yield:
                    ensure_active_coro(rel._thread, parts.at(2));
                    rel._coro = get_active_coro(rel._thread);
                    rel._rel_type = rel_yield{demangle(std::string(parts.at(3)))};
                    break;
                default:
                    throw 1;
            }
            if (rel._rel_type.index() == 0) {
                throw 2;
            }
            _relations.push_back(rel);
        }


    } catch (int x) {
        std::string msg = "Parse error at line: `" + b1 + "` argument " + std::to_string(x);
        throw std::runtime_error(msg);
    }
}

inline const std::string& App::get_active_coro(unsigned int thread) {
    return _thread_map[thread]._last_active;
}

inline void App::deactivate_coro(unsigned int thread, std::string_view id) {
    introduce_coro(id);
    auto &thr = _thread_map[thread];
    if (!thr._stack.empty()) {
        auto &s = thr._stack.top();
        auto f = std::find(s.begin(), s.end(), std::string(id));
        if (f != s.end()) {
            s.erase(f, s.end());
        }
        if (!s.empty()) thr._last_active = s.front();
        else thr._last_active = {};
    } else {
        thr._last_active = {};
    }
}

inline void App::activate_coro(unsigned int thread, std::string_view id) {
    introduce_coro(id);
    auto &thr = _thread_map[thread];
    if (!thr._stack.empty()) {
        auto &s = thr._stack.top();
        if (s.empty() || s.front() != id)  {
            s.push_back(std::string(id));
        }
    }
    thr._last_active = std::string(id);

}

inline void App::ensure_active_coro(unsigned int thread, std::string_view id) {
    introduce_coro(id);
    if (get_active_coro(thread) !=  id) {
       _relations.push_back(relation_t{
           thread, get_active_coro(thread), rel_unknown_switch{std::string(id)}
       });
       activate_coro(thread, id);
    }
}

inline void App::set_name(std::string_view id, std::string_view file,
        std::string_view name, std::string_view user) {
    auto &c = _coro_map[std::string(id)];
    c.file = std::string(file);
    c.name = std::string(name);
    c.user_data = std::string(user);

}

inline void App::set_type(std::string_view id, std::string_view type) {
    auto &c = _coro_map[std::string(id)];
    c.type = demangle(std::string(type));
}

inline void App::add_stack_level(unsigned int thread) {
    auto &thr = _thread_map[thread];
    thr._stack.push({});
}

inline void App::remove_stack_level(unsigned int thread) {
    auto &thr = _thread_map[thread];
    if (thr._stack.empty()) {
        thr._last_active = {};
    } else {
        thr._stack.pop();
        if (thr._stack.empty()) {
            thr._last_active = {};
        } else {
            auto &s = thr._stack.top();
            if (!s.empty()) {
                thr._last_active = s.back();
            }
        }
    }
}

inline coro_map_t::iterator App::introduce_coro(std::string_view id) {
    std::string str_id(id);
    auto iter = _coro_map.find(str_id);
    if (iter == _coro_map.end()) {
        return _coro_map.emplace(str_id, coro_info_t{str_id}).first;
    } else {
        return iter;
    }
}

inline void App::create_coro(std::string_view id, std::size_t sz) {
    auto iter = introduce_coro(id);
    if (iter->second._destroyed) {
        solve_conflict(std::string(id));
        introduce_coro(id)->second.size = sz;
    } else {
        iter->second.size = sz;
    }

}

inline void App::mark_destroyed(std::string_view id) {
    introduce_coro(id)->second._destroyed= true;
}

inline std::string App::demangle(const std::string &txt) {
    std::string out;
    std::size_t len = 0;
    int status = 0;
    char *c =  abi::__cxa_demangle(txt.data(), nullptr, &len, &status);
    if (status == 0) {
        out.append(c);
        std::free(c);
    } else {
        out = txt;
    }
    return out;
}

inline void App::solve_conflict(std::string id) {
    int pos = 1;
    std::string new_name;
    do {
        new_name = id + "_" + std::to_string(pos);
        auto iter =_coro_map.find(new_name);
        if (iter == _coro_map.end()) break;
        ++pos;
    } while (true);

    auto citer = introduce_coro(id);
    auto niter = introduce_coro(new_name);
    niter->second = citer->second;
    niter->second.id = new_name;
    citer->second = {};
    citer->second.id = id;
    for (auto &r: _relations) {
        if (r._coro == id) r._coro = new_name;
        std::visit([&](auto &item){
            if constexpr(has_target<decltype(item)>) {
                if (item._target == id) item._target = new_name;
            }
        }, r._rel_type);
    }
}



inline std::string thread_state_t::generate_label(unsigned int id) const {
    std::string z = _tid;
    for (char &c: z) {
        if (c == '"') c = '`';
        if (c >= 0 && c < 32) c = '.';
    }
    return "thread #" + std::to_string(id) + "\\n" + z;
}




static std::string strip_path(std::string_view where) {
    auto pos = where.find_last_of("\\/");
    if (pos == where.npos) return std::string(where);
    return std::string(where.substr(pos+1));
}

inline std::string coro_info_t::generate_label(bool nl) const {
    if (name.empty() && file.empty()) {
        return id;
    }
    std::string_view nlseq = nl?"\n":"\\n";

    std::string n;
    if (file.empty()) {
        n = name;
    } else if (name.empty()) {
        n = strip_path(file);
    } else {
        n = name+nlseq.data()+strip_path(file);
    }
    if (!user_data.empty()) {
        n.append(nlseq).append(user_data);
    }
    for (char &c: n) {
        if (c == '"') c = '`';
        if (nl && c == '\n') continue;
        if (c >=0 && c<32) c = '.';
    }
    return n;
}


void App::export_uml(std::ostream &out, unsigned int label_size) {
    out << "@startuml\n"
            "skinparam NoteTextAlignment center\n";

    for (const auto &[id,info]: _thread_map) {
        out << "control \"" << info.generate_label(id) << "\" as T" << id << "\n";
        out << "activate T" << id << "\n";
    }

    std::unordered_set<std::string_view> created_actors;


    for (auto &r: _relations) {
          std::visit([&](const auto &rel){
              using T = std::decay_t<decltype(rel)>;
              if constexpr(std::is_same_v<T, rel_create>) {
                  created_actors.insert(rel._target);
              }
          },r._rel_type);
    }

    for (const auto &[id,info]: _coro_map) {
        if (created_actors.find(id) == created_actors.end()) {
            out << "participant C" << id << "[\n"
                    << info.generate_label(true) << "\n"
                    << "----\n"
                    << short_label_size_template(info.type, label_size) << "\n"
                    << "]\n";
        }
    }
    auto node_name = [](unsigned int thread, std::string_view coro) {
        if (coro.empty()) return "T"+std::to_string(thread);
        else return std::string("C").append(coro);
    };


    std::unordered_map<std::string, std::string> _coro_suspend_notes;
    auto add_note = [&](const std::string &coro, const std::string &note) {
        _coro_suspend_notes[coro] = note;
    };
    auto flush_note = [&](const std::string &coro) {
        auto iter = _coro_suspend_notes.find(coro);
        if (iter != _coro_suspend_notes.end()) {
            out << "hnote over C" << coro << ": " << iter->second << std::endl;
            _coro_suspend_notes.erase(iter);
        }
    };


    for (auto &r: _relations) {


          std::visit([&](const auto &rel){
              using T = std::decay_t<decltype(rel)>;
              if constexpr(std::is_same_v<T, rel_create>) {
                  const auto &info = _coro_map[rel._target];
                  out << "create participant \"" << info.generate_label(false) << "\" as C" << rel._target << "\n";
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << ": create\n";
                  if (!rel._suspended) {
                      out << "activate " << node_name(r._thread, rel._target) << "\n";
                  }
                  if (!info.type.empty()) {
                      out << "note right : " << short_label_size_template(info.type, label_size) << "\n";
                  }
              } else if constexpr(std::is_same_v<T, rel_destroy>) {
                  switch (rel._type) {
                      default:
                      case rel_destroy::t_call:
                          out << node_name(r._thread, r._coro) << "->"  << node_name(r._thread, rel._target) << " !! : destroy \n";
                          break;
                      case rel_destroy::t_suspend:
                          out << node_name(r._thread, r._coro) << "->"  << node_name(r._thread, rel._target) << " --++ \n";
                          out << "destroy " << node_name(r._thread, r._coro) << "\n";
                          break;
                      case rel_destroy::t_return:
                          out << node_name(r._thread, rel._target) << "<-" <<  node_name(r._thread, r._coro) <<  " : destroy and return \n";
                          out << "destroy " << node_name(r._thread, r._coro) << " \n";
                          break;
                  }
              } else if constexpr(std::is_same_v<T, rel_hline>) {
                  out << "== ";
                  for (char c: rel._text) {
                      if (c == '\n') out << "\\n";
                      else if (c >= 0 && c < 32) out << '.';
                      else out << c;
                  }
                  out << " ==\n";
              } else if constexpr(std::is_same_v<T, rel_loop>) {
                  out << "loop " << rel.count << "x\n";
              } else if constexpr(std::is_same_v<T, rel_end_loop>) {
                  out << "end\n";
              } else if constexpr(std::is_same_v<T, rel_yield>) {
                  out << "hnote over " << node_name(r._thread, r._coro) << ": **co_yield** " << short_label_size_template(rel._type, label_size) << "\n";
              } else if constexpr(std::is_same_v<T, rel_suspend>) {
                  out << node_name(r._thread, rel._target) << "<-" << node_name(r._thread, r._coro) << ": suspend\n";
                  out << "deactivate " << node_name(r._thread, r._coro) << "\n";
                  flush_note(r._coro);
              } else if constexpr(std::is_same_v<T, rel_resume>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << ": resume\n";
                  out << "activate " << node_name(r._thread, rel._target) << "\n";
              } else if constexpr(std::is_same_v<T, rel_return>) {
                  out << node_name(r._thread, rel._target) << "<-" << node_name(r._thread, r._coro) << " : return\n";
                  if (!r._coro.empty()) out << "deactivate " << node_name(r._thread, r._coro) << "\n";
                  if (r._coro.empty() && rel._target.empty()) out << "deactivate " << node_name(r._thread, "") << "\n";
                  flush_note(r._coro);
              } else if constexpr(std::is_same_v<T, rel_await>) {
                  add_note(r._coro,"**co_await**\\n" + short_label_size_template(rel._type, label_size));
              } else if constexpr(std::is_same_v<T, rel_switch>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << " --++ \n";
                  flush_note(r._coro);
              } else if constexpr(std::is_same_v<T, rel_user_log>) {
                  out << "note over " << node_name(r._thread, r._coro) << ": **output**\\n " << rel.text << "\n";
              } else if constexpr(std::is_same_v<T, rel_unknown_switch>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target);
                  if (!r._coro.empty()) {
                     out << " --++";
                  } else {
                     out << " ++";
                  }
                  out <<" : <<unexpected>>\n";
                  flush_note(r._coro);
              } else {
                  out << "note over " << node_name(r._thread, r._coro) << ": **unknown state** " << demangle(typeid(T).name()) << "\n";
              }

          },r._rel_type);
      }


    out << "@enduml\n";
}


bool App::filter_active() {
    std::unordered_map<unsigned int, bool> not_relevant;
    _relations.erase(std::remove_if(_relations.begin(), _relations.end(),
            [&](const relation_t &rel)->bool{

        bool remove = false;

        int target_del = std::visit([&](const auto &item) {
            if constexpr(has_target<decltype(item)>) {
                if (item._target.empty()) return -1;
                auto iter = _coro_map.find(item._target);
                if (iter == _coro_map.end()) return -1;
                return iter->second._destroyed?1:0;
            } else {
                return -1;
            }
        },  rel._rel_type);

        if (target_del) {
            if (!rel._coro.empty()) {
                auto iter = _coro_map.find(rel._coro);
                if (iter != _coro_map.end()) {
                    remove = iter->second._destroyed;
                }
            } else if (target_del == 1){
                remove = true;
            } else {
                remove = not_relevant[rel._thread];
            }
        }
        not_relevant[rel._thread] = remove;
        return remove;

    }), _relations.end());

    if (_relations.empty()) return false;

    filter_actors();

    return true;
}

void App::filter_nevents(unsigned int n) {
    if (n >= _relations.size()) return;
    auto beg = _relations.begin();
    auto end = beg + _relations.size() - n;
    _relations.erase(beg,end);

    filter_actors();
}

void App::filter_actors() {

    std::unordered_set<unsigned int> threads;
    std::unordered_set<std::string_view> coros;

    for (const auto &rel: _relations) {
        threads.insert(rel._thread);
        coros.insert(rel._coro);
        std::visit([&](const auto &x){
            if constexpr(has_target<decltype(x)>) {
                coros.insert(x._target);
            }
        }, rel._rel_type);
    }

    for (auto iter = _thread_map.begin(); iter != _thread_map.end();) {
        if (threads.find(iter->first) == threads.end()) {
            iter = _thread_map.erase(iter);
        } else {
            ++iter;
        }
    }
    for (auto iter = _coro_map.begin(); iter != _coro_map.end();) {
        if (coros.find(iter->first) == coros.end()) {
            iter = _coro_map.erase(iter);
        } else {
            ++iter;
        }
    }

}


void App::set_thread(unsigned int thread, std::string_view id) {
    _thread_map[thread]._tid = std::string(id);
}

std::string App::short_label_size_template(std::string txt, unsigned int size) {
    while (txt.length() > size) {
        auto pos = txt.rfind('<');
        if (pos == txt.npos) break;
        auto end = txt.find('>', pos);
        if (end == txt.npos) {
            txt[pos] = '\x1E';
        } else {
            txt.replace(pos, end-pos+1, "\x1E...\x1F");
        }
    }
    for (char &c: txt) {
        if (c == '\x1E')  c = '<';
        else if (c == '\x1F')  c = '>';
    }
    return txt;
}

template<bool ignore_user>
void App::detect_loops() {
    while (detect_loop_cycle<ignore_user>());
}

template<bool ignore_user>
bool App::detect_loop_cycle() {

    auto compare = [](const relation_t &a, const relation_t &b) {
        if constexpr(ignore_user) {
            return a.equal_ignore_user(b);
        } else {
            return a == b;
        }
    };

    std::size_t max_len = _relations.size();
    std::size_t max_seq_len = max_len/2;
    for (std::size_t len = 1; len < max_seq_len; ++len) {
        for (std::size_t pos = 0; pos < (max_len-2*len); ++pos) {
            bool found_cycle = true;
            for (std::size_t i = 0; i < len; ++i) {
                if (!compare(_relations[pos+i],_relations[pos+i+len])) {
                    found_cycle = false;
                    break;
                }
            }
            if (found_cycle) {
                std::size_t count = 1;
                do {
                    ++count;
                    if (pos + (count+1)*len > max_len) break;
                    for (std::size_t i = 0; i < len; ++i) {
                        if (!compare(_relations[pos+i] ,_relations[pos+i+count*len])) {
                            found_cycle = false;
                            break;
                        }
                    }
                } while (found_cycle);

                auto iter = _relations.begin();
                std::advance(iter, pos);
                *iter = relation_t{
                    iter->_thread,{},rel_loop{count}
                };
                auto iter_end = iter;
                std::advance(iter_end,(count-1)*len);
                std::advance(iter,1);
                _relations.erase(iter, iter_end);
                std::advance(iter,len);
                _relations.insert(iter,relation_t{
                    0,{},rel_end_loop{count}
                });

                return true;
            }
        }
    }
    return false;
}

void print_help() {
    std::cerr << "Usage: program [-ah] [-ss count] [-f <file>] [-n count] [-o <file>]\n"
            << "  -f <file> input file name (default stdin)\n"
            << "  -o <file> output file name (default stdout)\n"
            << "  -a        all coroutines (include finished)\n"
            << "  -l        detect and collapse loops\n"
            << "  -L        detect and collapse loops ignore user data\n"
            << "  -s count  short labels up to characters (default=50)\n"
            << "  -n count  process only  last <count> events\n"
            << "  -h        show help\n";
}

int main(int argc, char *argv[]) {
    int opt;
    getopt_t getopt;
    bool process_all = false;
    bool show_help = false;
    bool collapse_loops = false;
    bool collapse_loops_ignore_user = false;
    std::string input_file;
    std::string output_file;
    int max_count = -1;
    int label_size = 50;

    while ((opt = getopt(argc, argv, "ahlLf:n:o:s:")) != -1) {
        switch (opt) {
            case 'a':
                process_all = true;
                break;
            case 'h':
                show_help = true;
                break;
            case 'f':
                input_file = getopt.optarg;
                break;
            case 'o':
                output_file = getopt.optarg;
                break;
            case 'l':
                collapse_loops = true;
                break;
            case 'L':
                collapse_loops_ignore_user = true;
                break;
            case 'n':
                max_count = std::atoi(getopt.optarg);
                break;
            case 's':
                label_size = std::atoi(getopt.optarg);
                break;
            default:
                std::cerr << getopt.errmsg << std::endl;
                print_help();
                return 1;
        }
    }

    if (show_help) {
        print_help();
        return 0;
    }

    App app;



    if (input_file.empty()) {
        app.parse(std::cin);
    } else {
        std::ifstream f(input_file);
        if (!f) {
            std::cerr << "Failed to open: " << argv[1] << std::endl;
            return 1;
        }
        app.parse(f);
    }
    if (!process_all) {
        if (!app.filter_active()) {
            std::cerr << "No active coroutines. To process whole file, specify -a" << std::endl;
            return 2;
        }
    }
    if (max_count > 0) {
        app.filter_nevents(max_count);
    }

    if (collapse_loops) {
        app.detect_loops<false>();
    }
    if (collapse_loops_ignore_user) {
        app.detect_loops<true>();
    }

    if (output_file.empty()) {
        app.export_uml(std::cout, label_size);
    } else {
        std::ofstream f(output_file, std::ios::trunc);
        if (!f) {
            std::cerr << "Failed to open: " << argv[1] << std::endl;
            return 1;
        }
        app.export_uml(f, label_size);
    }


    return 0;
}
