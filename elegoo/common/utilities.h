/***************************************************************************** 
 * @Author       : Ben
 * @Date         : 2025-04-19 16:50:24
 * @LastEditors  : Ben
 * @LastEditTime : 2025-06-27 14:25:07
 * @Description  : 
 * 
 * Copyright (c) 2025 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/

#pragma once

#include <string>
#include <vector>
#include <limits>
#include <unordered_map>
#include <cstdint>

constexpr int INT_NONE = std::numeric_limits<int>::min();
constexpr double DOUBLE_NONE = std::numeric_limits<double>::quiet_NaN();
constexpr double DOUBLE_INVALID = std::numeric_limits<double>::infinity();
enum class BoolValue { BOOL_FALSE = 0, BOOL_TRUE = 1, BOOL_NONE = 2 };

namespace elegoo
{
namespace common
{

// 实现 python strip()
std::string strip(const std::string& input, const std::string& chars = " \t\n\r\v\f");
// 字母转大写
std::string to_upper(const std::string &str);
// 实现 python split()
std::vector<std::string> split(const std::string& input, const std::string& delimiter = " ", size_t max_splits = std::string::npos);
// 实现 python shelx.split()
std::vector<std::string> shlex_split(const std::string& input);
// 实现 python 正则表达式分割
std::vector<std::string> regex_split(const std::string& input);
// 实现 python join()
std::string join(const std::vector<std::string>& elements, const std::string& delimiter);

void fix_sigint();

void set_nonblock(int fd);

void clear_hupcl(int fd);

int create_pty(const std::string& ptyname);

void dump_file_stats(const std::string& build_dir, const std::string& filename);

void dump_mcu_build();

std::string get_cpu_info();

std::string get_version_from_file(const std::string& elegoo_src);

void get_firmware_info(std::string path, std::unordered_map<std::string, std::string>& map);

std::string get_git_version(bool from_file = true);

bool are_equal(double a, double b, double epsilon = 1e-9);

int ffs(uint32_t mask);

int bit_length(int number);


} // namespace common
} // namespace elegoo