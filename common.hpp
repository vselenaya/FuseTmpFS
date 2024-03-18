#pragma once

#include <string>
#include <sys/stat.h>
#include <vector>
#include <string.h>

#include "rasserts.hpp"

using namespace std;


// Получаем текущее время:
static struct timespec get_curr_timespec() {
    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
    return curr_time;
}


// Получаем путь вида /... - в конце без '/' и начинающийся со '/' - те путь от корня ФС:
static string construct_path(const char *path) {
    string s = "";
    for (int i = 0; i < (int) strlen(path); i ++)
        s += path[i];
    if (s == "/" || s == "")
        return s;
    if (s[0] != '/')
        s = '/' + s;
    if (s[s.size()-1] == '/')
        s.pop_back();
    return s;
}


// Разбиваем строку str на куски по разделителю del:
static vector <string> str_split(string str, string del, bool ignore_empty=true) {
    size_t pos = 0;  // позиция, где разделитель
    vector <string> tokens;

    while (1) {
        pos = str.find(del);
        string cur_token = str.substr(0, pos);  // взяли текущей токен
        if (cur_token.size() > 0 || ignore_empty == 0)  // добавляем с вектор, если он не пустой или если мы НЕ игнорируем пустые токены
            tokens.push_back(cur_token);
        if (pos == string::npos)  // если так, то разделитель НЕ находится в строке
            break;
        str.erase(0, pos + del.length());  // спокойно меняем str, так как ф функцию передаётся её копия
    }
    return tokens;
}


// Функция из пути достаёт префикс и кончик - имя файла или директории, на которую path ведёт
static void get_prefix_and_name(const char *_path, string &prefix, string &name) {
    rassert(_path[0] == '/' && strlen(_path) > 1, "Невозможно разбить путь на префикс и имя!");
    string path = construct_path(_path);
    int del = path.rfind('/');
    prefix = path.substr(0, del);
    if (prefix == "")
        prefix = "/";
    name = path.substr(del+1, path.size());
}


// Проферяем корректность времени:
static bool check_tv(struct timespec tv) {
    if (tv.tv_nsec == UTIME_NOW || tv.tv_nsec == UTIME_OMIT)
        return 1;
    //if (tv.tv_sec < 0 || tv.tv_sec > 999999999 || tv.tv_nsec < 0 || tv.tv_nsec > 999999999)
     //   return 0;
    return 1;
}