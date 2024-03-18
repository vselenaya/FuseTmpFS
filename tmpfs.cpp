#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#define FUSE_USE_VERSION 26
#define HAVE_SYS_XATTR_H 0
#include <fuse.h>
#include <errno.h>
#include <string.h>

#include <vector>
#include <string>
#include <iostream>
#include <map>

#include "rasserts.hpp"
#include "common.hpp"


#define PREFIX_IS_NOT_DIR -2  // ошибка, означающая, что префикс пути - не директория
#define PATH_NOT_FOUND -1  // ошибка, означающая, что путь не найден
#define TMPFS_DATA ((TableInodes *) fuse_get_context()->private_data)  // достаём данные, которые передавали в функции fuse_main

using namespace std;



// === Структура данных, хранящихся в каталоге (= директории) нашей файловой системы === 
struct catalog_data {
    map <string, int> files; // словарь пар: (имя файла/директории, его номер inode) -> доступ, удаление добавление в словарь - за O(log n)
    size_t count;  // количество файлов в каталоге

    catalog_data() {
        count = 0;
    }

    void add_file(string name, int num_inode) {  // добавляем файл (или под-директорию) name с номером num_inode
        rassert(files.count(name) == 0, "Попытка добавить в каталог существующий файл!");
        files.insert({name, num_inode});
        count += 1;
        return;
    }

    void delete_file(string name) {  // удаляем файл (или под-директорию) с номером
        rassert(files.count(name) == 1, "Попытка удалить из каталога несуществующий файл");
        files.erase(name);
        count -= 1;
    }
};



// === Структура данных файла ===
struct file_data {
    vector <uint8_t> data;  // данные - это вектор из байт = uint8_t
    size_t size;  // количество байт данных

    file_data() {
        size = 0;
    }

    void add_byte(size_t i, uint8_t byte) {  // добавляем байт по индексу (=по смещению от начала файла) i в вектор 
        rassert(i <= size, "Попытка писать за файлом!");
        
        if (i == size) {
            data.push_back(byte);
            size += 1;
        } else {
            data[i] = byte;
        }
    }
};



// === Структура для хранения самой Inode - единицы в нашей файловой системе ===
struct INODE {
    int num;  // номер Inode
    void *data;  // указатель на данные - либо на catalog_data, либо file_data
    INODE *par;  // указатель на родителя (на каталог, где находится данная вершина)
    mode_t mode;  // модификатор доступа
    nlink_t nlink;  // количество ссылок 
    uid_t uid;  // владелец и группа владельца
    gid_t gid;
    int opened_by;  // количество открытий 

    struct timespec st_atim;  // время последнего доступа к файлу (чтения его и тд) или содержимому директории;
                              // если мы просто удаляем файл из директории, это не меняем atim, тк как содержимое директории не было прочитано;
                              // смысл atim, чтобы гарантированно узнать, когда кто-либо узнавал что-нибудь о директории/файле... если из директории удаляется/или в неё добавляется файл - это не раскрывает содержимое директории -> время не меняется
    struct timespec st_mtim;  // изменения соедержания (поля data - запись в файл или дрбавление/удаление нового файла в директорию)
    struct timespec st_ctim;  // изменение метаданных файла: прав доступа, числа ссылок и тд - НО измение любого из времён atim и mtim - НЕ имзмение данных -> не меняется и ctim
    // ! не уверен, насколько в стандартных файловых системах linux интерпретация времён atim, mtim, ctim совпадает с той, что тут... но тут звучит логично !

    
    void update_time(bool atim, bool mtim, bool ctim) {  // обновляем время inode:
        st_atim = atim ? get_curr_timespec() : st_atim;
        st_mtim = mtim ? get_curr_timespec() : st_mtim;
        st_ctim = ctim ? get_curr_timespec() : st_ctim;
    }
  
    bool check_mode(bool R, bool W, bool X) {  // проверяем права доступа: возвращаем 1 если все указанные права R (чтение), W (запись), X (запуск) разрешены данному пользователю
        uid_t curr_uid = fuse_get_context()->uid;  // получаем данные о пользователе, для которого ужно проверить права
        gid_t curr_gid = fuse_get_context()->gid;

        if (curr_uid == 0 || curr_gid == 0)  // если пользователь - root, то ему всё можно
            return 1;

        mode_t Ra, Wa, Xa;  // здесь сохраняем маски, с помощью применения которых будем проверять права доступа
        if (curr_uid == uid) {  // если текущий пользователь = пользователю-владельцу, то маски такие:
            Ra = S_IRUSR;  // маска = возможность читать владельцу файла... см man 2 chmod, например
            Wa = S_IWUSR;
            Xa = S_IXUSR;
        } else if (curr_gid == gid) {  // если группа текущего пользователя совпадает с группой фала
            Ra = S_IRGRP;
            Wa = S_IWGRP;
            Xa = S_IXGRP;
        } else {  // иначе текущий пользователь относится к "остальным" - ему маски такие:
            Ra = S_IROTH;
            Wa = S_IWOTH;
            Xa = S_IXOTH;
        }

        bool res = 1;
        if (R)  // если нужно проверять права на чтение, проверяем:
            res = res && (mode & Ra);
        if (W)
            res = res && (mode & Wa);
        if (X)
            res = res && (mode & Xa);
        return res;  // возвращаем результат проверок
    }

    INODE() {
        num = 0;
        opened_by = 0;
        nlink = 0;
        par = NULL;
        mode = 0;  // устаавливаем в 0 изначально - это значит, что пока эта inode - свободна: вообще ничего
    }

    ~INODE() {
        if (mode == 0)  // если inode - ничего, не удаляем
            return;

        if (S_ISDIR(mode) == 1)
            delete ((catalog_data *) data);  // очищаем данные Inode (в зависимости от того, файл или директория)
        else if (S_ISREG(mode) == 1)
            delete((file_data *) data);
        else
            rassert(0, "Неизвестный тип Inode - попытка удаления!");
    }
};



// === Структура для хранения в памяти созданных Inode ===
struct TableInodes {
    vector <INODE*> inodes;  // тут лежат указатели на все созданные Inode - фактически это вся Файловая система + запас Inode для новых файлов
    vector <int> free_inodes;  // тут лежат индексы (и они же номер Inode) тех Inode, которые в данный момент свободны - то есть созданы, но не задействованы в файловой система 
    size_t N;  // полное колиество inode

    TableInodes() {
        N = 10;
        for (size_t i = 0; i < N; i ++)  // создаём N вершин
            inodes.push_back(new INODE());
        for (size_t i = 1; i < N; i ++)  // свободны все, кроме 0-ой Inode
            free_inodes.push_back(i);
        
        inodes[0]->uid = getuid();  // 0-ая Inode - это корень нашей файловой системы (он совпадает с той папкой, к которой монтируем файловую систему при запуске)
        inodes[0]->gid = getgid();
        inodes[0]->mode = 0777 | S_IFDIR;
        catalog_data *data = new catalog_data();  // создаём структуру для корневой директории
        inodes[0]->data = data;
        data->add_file(".", 0);
        data->add_file("..", 0);  // . и .. в корневой директории ссылаются на саму себя
        inodes[0]->num = 0;
        inodes[0]->update_time(1, 1, 1);  // в момент создания всё времена устанавливаются!
    }

    void resize() {  // если текущее количество Inode не хватает - добавляем ещё
        for (size_t i = N; i < 2 * N; i ++) {
            inodes.push_back(new INODE());
            free_inodes.push_back(i);
        }
        N *= 2;
    }

    int new_inode() {
        if (free_inodes.size() == 0)
            resize();
        int num = free_inodes[free_inodes.size()-1];  // берём свободный индекс - теперь это номер новой Inode
        free_inodes.pop_back();  // убираем индекс из свободных
        return num;
    }

    void delete_inode(int num_inode) {  // удаляем inode по номеру
        delete inodes[num_inode];  // удаляем старую Inode
        inodes[num_inode] = new INODE();  // создаём взамен новую, свободную
        free_inodes.push_back(num_inode);  // возвращаем номер в список свободных inode
    }
    
    ~TableInodes() {
        for (size_t i = 0; i < N; i ++)
            delete inodes[i];
    }
};


// По пути _path внутри нашей ФС получаем номер inode, которая соответствует пути:
static int get_num_inode_by_path(const char *_path) {
    string path = construct_path(_path);  // получаем путь в удобном виде
    if (path == "/")  // если путь - просто корень, то номер корня - это 0:
        return 0;

    int curr_num_inode = 0;  // пока что текущий inode - 0-ой (то есть корневая директория)

    for (string token: str_split(path, "/")) {  
        INODE *inode = TMPFS_DATA->inodes[curr_num_inode];  // получаем текущую inode

        if (S_ISDIR(inode->mode) == 0)
            return PREFIX_IS_NOT_DIR;  // префикс пути - не директория

        catalog_data *data = (catalog_data *) inode->data;
        if (data->files.count(token) == 0)
            return PATH_NOT_FOUND;  // не нашли name -> некорректный путь

        curr_num_inode = data->files[token];  // переходим по пути к следующей inode -> её номер берём
    }

    return curr_num_inode;
}


// Проверяем, что на всех директориях в пути _path есть разрешение X - оно нужно для того, чтобы хотя бы войти в директорию и что-нибдь там сделать!
// в данной функции считаем, что все элементы path - существующие директории!
static bool check_X_in_path(const char *path, bool start_from_prefix=0) {  
    int num = get_num_inode_by_path(path);
    rassert (num >= 0, "Путь должен быть корректным в проверке X бита!");
    INODE *inode = TMPFS_DATA->inodes[num];

    if (start_from_prefix == 1)
        inode = inode->par;  // наичнаем не с начала пути, а с предыдущей директрории

    while (inode != NULL) {
        rassert(S_ISDIR(inode->mode) == 1, "Все чати пути должны быть директории в X битовой проверке!");
        if (inode->check_mode(0, 0, 1) == 0)
            return 0;
        inode = inode->par;
    }
    return 1;
}


// Функция для создания директории (вызывается при вызове команды mkdir, например):
int tmpfs_mkdir(const char *_path, mode_t mode) {
    if (get_num_inode_by_path(_path) >= 0)
        return -EEXIST;  // путь уже есть (необязательно директория)

    string prefix, dir;
    get_prefix_and_name(_path, prefix, dir);

    int num = get_num_inode_by_path(prefix.c_str());
    if (num == PATH_NOT_FOUND)
        return -ENOENT;  // директория, где нужно создать новую диреткорию - не существует
    if (num == PREFIX_IS_NOT_DIR || S_ISDIR(TMPFS_DATA->inodes[num]->mode) == 0)
        return -ENOTDIR;  // кусочек пути - не является директорией
    if (check_X_in_path(prefix.c_str()) == 0 || TMPFS_DATA->inodes[num]->check_mode(0, 1, 0) == 0)
        return -EACCES;  // в пути нет X-бита бита, нет права на запись в родительской директории

    // теперь у нас еть prefix-директория с номером inode = num - в ней мы создаём директорию dir
 
    int new_num = TMPFS_DATA->new_inode();  // создали новую inode для директории dir
    INODE* new_inode = TMPFS_DATA->inodes[new_num];
    INODE *pref_inode = TMPFS_DATA->inodes[num];

    catalog_data *new_data = new catalog_data();  // данные dir
    new_data->add_file(".", new_num);
    new_data->add_file("..", num);
    pref_inode->nlink += 1;  // в prefi директорию добавилась ссылка ".."

    new_inode->data = new_data;
    new_inode->nlink = 2;  // изначально 2 ссылки - из prefix-директории и "." - указывает на саму же директорию
    new_inode->mode = (mode & ~fuse_get_context()->umask) | S_IFDIR;  // отмечаем, что данная inode - директория, устанавливаем разрешения с учётом umask
    new_inode->uid = fuse_get_context()->uid;
    new_inode->gid = fuse_get_context()->gid;
    new_inode->num = new_num;
    new_inode->par = pref_inode;
    new_inode->update_time(1, 1, 1);  // устанвливаем время - при создании

    ((catalog_data *) pref_inode->data)->add_file(dir, new_num);  // добавили в prefix директорию новую
    pref_inode->update_time(0, 1, 1);  // в prefix-директории появился новый файл -> время изменено: mtim меняется, так как жанные директории изменены - новый файл, ctim меняется, так как меняется счётчик файлов...

    return 0;
}


// Функция создания файла (вызывается при touch, например):
int tmpfs_mknod(const char *_path, mode_t mode, dev_t dev) {
    (void) dev;

    if (get_num_inode_by_path(_path) >= 0)
        return -EEXIST;

    string prefix, file;
    get_prefix_and_name(_path, prefix, file);

    int num = get_num_inode_by_path(prefix.c_str());
    if (num == PATH_NOT_FOUND)
        return -ENOENT;  // кусочек пути не существует
    if (num == PREFIX_IS_NOT_DIR || S_ISDIR(TMPFS_DATA->inodes[num]->mode) == 0)
        return -ENOTDIR;
    if (check_X_in_path(prefix.c_str()) == 0 || TMPFS_DATA->inodes[num]->check_mode(0, 1, 0) == 0)
        return -EACCES;  // в пути нет X-бита бита, нет права на запись в родительской директории

    int new_num = TMPFS_DATA->new_inode();  // создали новую inode для файла file
    INODE* new_inode = TMPFS_DATA->inodes[new_num];
    INODE *pref_inode = TMPFS_DATA->inodes[num];  // prefix-директория, где файл создаётся

    new_inode->data = new file_data();  // данные dir
    new_inode->nlink = 1;  // изначально 1 ссылка - из prefix-директории
    new_inode->mode = (mode & ~fuse_get_context()->umask) | S_IFREG;  // отмечаем, что данная inode - это регулярный файл
    new_inode->uid = fuse_get_context()->uid;
    new_inode->gid = fuse_get_context()->gid;
    new_inode->num = new_num;
    new_inode->par = pref_inode;
    new_inode->update_time(1, 1, 1); 

    ((catalog_data *) pref_inode->data)->add_file(file, new_num);  // добавили в текущую директорию новый файл
    pref_inode->update_time(0, 1, 1);

    return 0;
}


// Создаём жёсткую ссылку по пути _newpath на файл на _path:
int tmpfs_link(const char *_path, const char *_newpath) {
    if (get_num_inode_by_path(_newpath) >= 0)
        return -EEXIST;

    string prefix, file;
    get_prefix_and_name(_newpath, prefix, file);

    int oldnum = get_num_inode_by_path(_path);  // номер inode файла, на который создаём ссылку
    int num = get_num_inode_by_path(prefix.c_str());  // директория, где создаём ссылку
    if (num == PATH_NOT_FOUND || oldnum == PATH_NOT_FOUND)
        return -ENOENT;  // компонента одного из путей - не найдена
    if (num == PREFIX_IS_NOT_DIR || oldnum == PREFIX_IS_NOT_DIR || S_ISDIR(TMPFS_DATA->inodes[num]->mode) == 0)
        return -ENOTDIR;  // компонента пути - не директория или prefix-путь не директория (хотя в нём ссылку нужно соаздать)
    if (S_ISDIR(TMPFS_DATA->inodes[oldnum]->mode) == 1)
        return -EPERM;  // oldpath - директория
    if (check_X_in_path(_path, 1) == 0 || check_X_in_path(prefix.c_str()) == 0 || TMPFS_DATA->inodes[num]->check_mode(0, 1, 0) == 0)
        return -EACCES; // нет X бита в пути _path ИЛИ нет X в пути _newpath ИЛИ  нельдя писать в директорию в _newpath -> тогда нет доступа, см man 2 link

    INODE *pref_inode = TMPFS_DATA->inodes[num];  // текущая директория, куда добавится ссылка
    ((catalog_data *) pref_inode->data)->add_file(file, oldnum);  // добавили в текущую директорию ссылку с именем file на старую inode old_num
    pref_inode->update_time(0, 1, 1);

    TMPFS_DATA->inodes[oldnum]->nlink += 1;  // увеличиваем число жёстких ссылок на файл
    TMPFS_DATA->inodes[oldnum]->update_time(0, 0, 1);  // метаданные изменили -> меняем время

    return 0;
}


// Функция получения информации (вызывается при ls -l, напрмимер):
int tmpfs_getattr(const char *path, struct stat *statbuf) {
    if (path[0] == 0)
        return -ENOENT;  // возвращаем -errno: значение ENOENT (согласно man 2 stat) - значит, что путь path - пустая строка (то есть сразу идёт нулевой байт - символ конца строки)

    int num = get_num_inode_by_path(path);
    if (num == PATH_NOT_FOUND)
        return -ENOENT;  // некорректный путь
    if (num == PREFIX_IS_NOT_DIR)
        return -ENOTDIR;  // префикс- не директория
    if (check_X_in_path(path, 1) == 0)
        return -EACCES;  // нет X бита в префиксе пути... при этом, если на самом файле/директории нет бита, то ничего страшного

    INODE *inode = TMPFS_DATA->inodes[num];
    inode->update_time(1, 0, 0);  // получили доступ к файлу/директории -> обновили время

    statbuf->st_mode = inode->mode;
    statbuf->st_nlink = inode->nlink;
    statbuf->st_uid = inode->uid;
    statbuf->st_gid = inode->gid;
    statbuf->st_ino = (ino_t) inode->num;
    statbuf->st_atim = inode->st_atim;
    statbuf->st_mtim = inode->st_mtim;
    statbuf->st_ctim = inode->st_ctim;

    if (S_ISDIR(inode->mode) == 1)
        statbuf->st_size = (off_t) ((catalog_data *) inode->data)->count;  // размер директории = количество файлов/ссылок в ней
    else if (S_ISREG(inode->mode) == 1)
        statbuf->st_size = (off_t) ((file_data *) inode->data)->size;  // размер файла - количетсво байт в нём
    
    return 0;
}


// Функция, которая открывает директорию:
int tmpfs_opendir(const char *path, struct fuse_file_info *fi) {
    if (path[0] == 0)
        return -ENOENT;  // имя - пустая строка
    
    int num = get_num_inode_by_path(path);
    if (num < 0)
        return -ENOENT;
    
    if (check_X_in_path(path, 1) == 0)
        return -EACCES;
    
    INODE *inode = TMPFS_DATA->inodes[num];
    
    if (S_ISDIR(inode->mode) == 0)
        return -ENOTDIR;  // путь - не директория
    
    fi->fh = num;  // сохраняем в структуре inode номер открытой директории
    inode->opened_by += 1;

    return 0;
}  


// Функция, которая прочитывает директорию (при ls вызывается):
int tmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
	              struct fuse_file_info *fi) {
    (void) path; (void) offset;

    int num = fi->fh;  // берём номер inode директории - его знаем по opendir
    INODE *inode = TMPFS_DATA->inodes[num];
    if (inode->check_mode(1, 0, 0) == 0)
        return -EACCES;

    if (S_ISDIR(inode->mode) == 0)
        return -ENOTDIR;  // путь - не директория

    for (auto pairs: ((catalog_data *) inode->data)->files)
        filler(buf, pairs.first.c_str(), NULL, 0);  // добавляем имеющийся файл в функцию

    inode->update_time(1, 0, 0);  // только лишь получаем доступ, метаданные не меняеются: opened_by - не метаданные, а внутренний счётчик... -> меняем только atim

    return 0;
}


// Функция, которая закрывает директорию:
int tmpfs_closedir(const char *path, struct fuse_file_info *fi) {
    (void) path;

    int num = fi->fh;
    INODE *inode = TMPFS_DATA->inodes[num];
    inode->opened_by -= 1;

    return 0;
}


// Функция удаления файла:
int tmpfs_unlink(const char *path) {
    if (path[0] == 0)
        return -ENOENT;
    int num = get_num_inode_by_path(path);
    if (num == PATH_NOT_FOUND)
        return -ENOENT;  // некорректный путь
    if (num == PREFIX_IS_NOT_DIR)
        return -ENOTDIR;  // префикс- не директория

    INODE *inode = TMPFS_DATA->inodes[num];
    if (S_ISDIR(inode->mode) == 1)
        return -EISDIR;  // путь - директория
    if (check_X_in_path(path, 1) == 0 || inode->par->check_mode(0, 1, 0) == 0)
        return -EACCES;  // нет права на исполнение в пути или нет права на запись в директории

    string _, name;
    get_prefix_and_name(path, _, name);

    ((catalog_data *) inode->par->data)->delete_file(name);  // удаляем файл из родительского каталога
    inode->nlink -= 1;  // удаляем файл = уменьшаем количетсво ссылок (так как "имя файла" в директории - тоже жёсткая ссылка) на него
    inode->par->update_time(0, 1, 1);  // в родительском каталоге удалился файл -> обновляем время
    inode->update_time(0, 0, 1);  // файл не читали, а лишь изменили метаданные - кол-во ссылок

    if (inode->opened_by == 0 && inode->nlink == 0)  // если файл не открыт и на файл не ссылается -> очищаем память
        TMPFS_DATA->delete_inode(num);
    
    return 0;
}


// Функция удаления директории:
int tmpfs_rmdir(const char *path) {
    int num = get_num_inode_by_path(path);
    if (num == PATH_NOT_FOUND)
        return -ENOENT;  // некорректный путь
    if (num == PREFIX_IS_NOT_DIR)
        return -ENOTDIR;  // префикс- не директория

    INODE *inode = TMPFS_DATA->inodes[num];
    if (S_ISDIR(inode->mode) == 0)
        return -ENOTDIR;  // путь - не директория
    if (check_X_in_path(path, 1) == 0 || inode->par->check_mode(0, 1, 0) == 0)
        return -EACCES;

    if (((catalog_data *) TMPFS_DATA->inodes[num]->data)->count > 2)  // если в директории ссылок > 2 (есть что-то кроме . и ..)
        return -ENOTEMPTY;  // не пустая директория

    string _, name;
    get_prefix_and_name(path, _, name);

    ((catalog_data *) inode->par->data)->delete_file(name);
    inode->par->update_time(0, 1, 1);
    TMPFS_DATA->delete_inode(num);

    return 0;
}


// Переименовываем файл/каталог (вызываается при mv, например):
/* oldpath - путь до файла/каталога, корторый есть -> newpath - новый путь (включая новое имя файла),
по которому должен располагаться этот файл/каталог (то есть переименование может не только изменить имя файла,
но и его расположение); заметим, что по newpath - может либо ничего не быть (тогда файл/директория создаётся),
либо быть файл (тогда он перезатирается независимо от того, что за файл), либо директория (она тоже перезатирается,
но только если она была пустой)*/
int tmpfs_rename(const char *oldpath, const char *newpath) {
    if (oldpath[0] == 0 || newpath[0] == 0)
        return -ENOENT;  // путь - пустая строка

    int oldnum = get_num_inode_by_path(oldpath);
    if (oldnum == PATH_NOT_FOUND)
        return -ENOENT;  // объекта по oldpath нет -> нечего переименовывать
    if (oldnum == PREFIX_IS_NOT_DIR)
        return -ENOTDIR;  // кусочек пути в olpath - не директория... напрмиер есть файл /dir1/file, а мы пытаемся использовать путь oldpath = dir1/file/dir2
    
    if (!strncmp(oldpath, newpath, strlen(oldpath)) && newpath[strlen(oldpath)] == '/') {
        return -EINVAL;  // старый путь - префикс нового!
    }

    INODE *inode = TMPFS_DATA->inodes[oldnum];  // получаем inode того, что переименовываем

    int newnum = get_num_inode_by_path(newpath);
    if (oldnum == newnum)  // если оба пути - на один и тот же файл (например, оба жесткие ссылки на одинаковый), или если пути совпадают - ничего делать не надо
        return 0;
    if (newnum == PREFIX_IS_NOT_DIR)
        return -ENOTDIR;  // компонента пути - не директория (но при этом существует!)...
    if (newnum >= 0) {  // если новый путь уже существует, то проверяем:
        INODE* curr = TMPFS_DATA->inodes[newnum];  // то, что на данный момент существует
        if (S_ISDIR(curr->mode) == 1 && S_ISDIR(inode->mode) == 0)
            return -EISDIR;  // newpath - директория, но oldpath - НЕ диреткория
        if (S_ISDIR(curr->mode) == 0 && S_ISDIR(inode->mode) == 1)
            return -ENOTDIR;  // тут наоборот
        if (S_ISDIR(curr->mode) == 1 && ((catalog_data *) curr->data)->count > 2)
            return -ENOTEMPTY;  // если newpath - существующая директория, но не пустая - те есть что-то кроме . и .., то тоже ошибка, чтобы не перезаписать существующую директорию!
                                // с файлами вот такого нет... если файл newpath существует и даже не пустой, то никакой ошибки - просто перезаписывается
    }

    string prefix, name;
    get_prefix_and_name(newpath, prefix, name);
    int newpref = get_num_inode_by_path(prefix.c_str());  // получаем директорию, где должен располагаться переименованный на name файл из oldpath
    if (newpref == PATH_NOT_FOUND)
        return -ENOENT;  // если пути нет, ошибка... newpath должен существовать весь до последнего кусочка - до name! а уже name мы либо создадим, если его не было в prefix-директории, либо перезапишем
                         // то есть в данном случае нет компоненты-директории в пути newpath -> ошибка ENOENT;
                         // это надо отличать от ситуации, когда newnum == PREFIX_IS_NOT_DIR -> это значало, что компонента-директория есть, но НЕ является директорией -> тогда ошибка ENOTDIR

    rassert(newpref >= 0, "Странно, вроде уже проверяли существование");  // так как newpath уже проверили и на то, что кусочек пути - директория - это проверяли в newnum == PREFIX_IS_NOT_DIR, и на то, что prefix - существует - только что проверили, то теперь точно prefix - существующая директория
    INODE *prefdir = TMPFS_DATA->inodes[newpref];  // то, где должен быть переименованный файл name
    if (check_X_in_path(oldpath, 1) == 0 || check_X_in_path(prefix.c_str(), 0) == 0 ||
        TMPFS_DATA->inodes[newpref]->check_mode(0, 1, 0) == 0 || TMPFS_DATA->inodes[oldnum]->par->check_mode(0, 1, 0) == 0 ||
        (S_ISDIR(TMPFS_DATA->inodes[oldnum]->mode) == 1 && TMPFS_DATA->inodes[oldnum]->check_mode(0, 1, 0) == 0))
        return -EACCES;  // нет X бита в путях ИЛИ нет права на запись в директориях ИЛИ переименуемая штука - директория, в которой нет права записи - нужно для обнолвения .. ссылки

    if (newnum >= 0) {  // если name уже существует и до этого не вызвал ошибок, значит мы его перезаписываем -> для начала просто удаляем
        INODE* curr = TMPFS_DATA->inodes[newnum];
        ((catalog_data *) curr->par->data)->delete_file(name);  // удаляем запись о файле из prefix
        curr->nlink -= 1;  // удаляем файл = уменьшаем количетсво ссылок на него
        if (S_ISDIR(curr->mode) == 1)
            TMPFS_DATA->delete_inode(newnum);  // если это директория, а мы знаем, что она пустая, то удаляем сразу
        if (S_ISREG(curr->mode) == 1 && curr->opened_by == 0 && curr->nlink == 0)  // если это файл и он не открыт и на него не ссылается -> тоже очищаем память
            TMPFS_DATA->delete_inode(newnum);
    }

    ((catalog_data *) prefdir->data)->add_file(name, oldnum);  // теперь добавляем в директорию то, что переименовывали с новым именем
    
    string _, oldname;
    get_prefix_and_name(oldpath, _, oldname);
    ((catalog_data *) inode->par->data)->delete_file(oldname);  // удаляем запись о файле из старой директории - так как файл переименовали

    inode->par->update_time(0, 1, 1);  // обновили время в старой директории
    prefdir->update_time(0, 1, 1);  // обновили время в новой
    
    inode->par = prefdir;  // !!! Важно!!! не забыли обновить предка директории! до этого была ошибка после команд mkdir -p 1/2/3/4/5, mv 1/2/3 ., -> вроде в / две директории: 1 и 3, но удаление rm -r 3 давало ошибку!, так как par старый был
    if (S_ISDIR(inode->mode) == 1) {
        ((catalog_data *) inode->data)->delete_file("..");
        ((catalog_data *) inode->data)->add_file("..", newpref);  // обновляем .. в директории! - просто предабавляем
    }
    inode->update_time(0, 0, 1);
    return 0;
}


// Функция открытия файла:
int tmpfs_open(const char *path, struct fuse_file_info *fi) {
    int num = get_num_inode_by_path(path);
    if (num == PREFIX_IS_NOT_DIR)
        return -ENOTDIR;
    if (num == PATH_NOT_FOUND)
        return -ENOENT;  // нет файла, или нет компоненты пути
    if (check_X_in_path(path, 1) == 0)
        return -EACCES;

    INODE *inode = TMPFS_DATA->inodes[num];

    if (S_ISDIR(inode->mode) == 1)
        return -EISDIR;  // путь на директорию!
    
    fi->fh = num;  // сохраняем в структуре
    inode->opened_by += 1;
    
    return 0;
}


// Функция чтения из файла (вызывается, когда, например, команда cat):
int tmpfs_pread(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) path;

    INODE* inode = TMPFS_DATA->inodes[fi->fh];
    file_data *data = (file_data *) inode->data;
    if (inode->check_mode(1, 0, 0) == 0)
        return -EACCES;
    size_t ind = 0;
    for (size_t i = offset; i < data->size; i ++) {  // читаем начиная с offset до конца файла
        if (ind == size)  // если уже прочиали сколько нужно -> останавливаем
            break;
        buf[ind] = (char) data->data[i];
        ind += 1;
    }

    inode->update_time(1, 0, 0);

    return ind;  // кол-во считанных байт
}


// Функция записи в файл (когда через nano редактируем, например):
int tmpfs_pwrite(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) path;

    INODE* inode = TMPFS_DATA->inodes[fi->fh];
    file_data *data = (file_data *) inode->data;
    if (inode->check_mode(0, 1, 0) == 0)
        return -EACCES;
    rassert(offset <= (off_t) data->size, "Пишем за концом файла!");

    size_t ind = 0;
    for (size_t i = offset; ; i ++) {
        if (ind == size)
            break;
        data->add_byte(i, buf[ind]);
        ind += 1;
    }

    inode->update_time(0, 1, 1);

    return ind;  // кол-во записанных байт
}


// Закрываем файл:
int tmpfs_close(const char *path, struct fuse_file_info *fi) {
    (void) path;

    int num = fi->fh;
    INODE *inode = TMPFS_DATA->inodes[num];
    inode->opened_by -= 1;
    inode->st_atim = inode->st_ctim = get_curr_timespec();

    if (inode->opened_by == 0 && inode->nlink == 0)  // если файл не открыт и ссылок нет (то есть ни в какой директории файла нет), удаляем!
        TMPFS_DATA->delete_inode(num);
	
    return 0;
}


// Делаем файл строго размера = newsize:
int tmpfs_truncate(const char *path, off_t newsize) {
    int num = get_num_inode_by_path(path);
    if (num == PREFIX_IS_NOT_DIR)
        return -ENOTDIR;
    if (num == PATH_NOT_FOUND)
        return -ENOENT;

    INODE *inode = TMPFS_DATA->inodes[num];
    if (check_X_in_path(path, 1) == 0 || inode->check_mode(0, 1, 0) == 0)
        return -EACCES;

    if (S_ISDIR(inode->mode) == 1)
        return -EISDIR;

    file_data *data = (file_data *) inode->data;
    data->data.resize(newsize);
    data->size = newsize;

    inode->update_time(0, 1, 1);

    return 0;
}


// Обновляем время в файле:
int tmpfs_utimens(const char *path, const struct timespec *tv) {
    if (path[0] == 0)
        return -ENOENT;

    int num = get_num_inode_by_path(path);
    if (num == PATH_NOT_FOUND)
        return -ENOENT;
    if (num == PREFIX_IS_NOT_DIR)
        return -ENOTDIR;
    INODE* inode = TMPFS_DATA->inodes[num];
    
    if (((tv == NULL || (tv[0].tv_nsec == UTIME_NOW && tv[1].tv_nsec == UTIME_NOW)) && inode->uid != fuse_get_context()->uid) ||
        inode->check_mode(0, 1, 0) == 0)
        return -EACCES;

    if (tv == NULL) {  // см документацию utimensat(2)
        inode->st_atim = inode->st_mtim = get_curr_timespec();
        return 0;
    }

    if (!(check_tv(tv[0]) && check_tv(tv[1])))
        return -EINVAL;  // некорректное временное значение

    if (tv[0].tv_nsec == UTIME_NOW) {  // устанавливаем ткущее время
        inode->st_atim = get_curr_timespec();
    } else if (tv[0].tv_nsec == UTIME_OMIT) {  // не меняем время
        (void) inode;
    } else {
        inode->st_atim = tv[0];
    }
    
    if (tv[1].tv_nsec == UTIME_NOW) {
        inode->st_mtim = get_curr_timespec();
    } else if (tv[1].tv_nsec == UTIME_OMIT) {
        (void) inode;
    } else {
        inode->st_mtim = tv[1];
    }

    return 0;
}


// Функция для изменния прав доступа (при chmod вызывается)
int tmpfs_chmod(const char *path, mode_t mode) {
    int num = get_num_inode_by_path(path);
    if (num == PATH_NOT_FOUND)
        return -ENOENT;
    if (num == PREFIX_IS_NOT_DIR)
        return -ENOTDIR;
    if (check_X_in_path(path, 1) == 0)
        return -EACCES;
    
    INODE *inode = TMPFS_DATA->inodes[num];
    
    if (inode->uid != fuse_get_context()->uid && (fuse_get_context()->uid != 0 || fuse_get_context()->gid != 0))
        return -EPERM;  // вызывающий функцию - не владелец и не привелегированный

    if (S_ISDIR(inode->mode) == 1)
        inode->mode = mode | S_IFDIR;
    if (S_ISREG(inode->mode) == 1)
        inode->mode = mode |= S_IFREG;

    return 0;
}


// Функция для изменения владельца:
int tmpfs_chown(const char *path, uid_t uid, gid_t gid) {
    int num = get_num_inode_by_path(path);
    if (num == PATH_NOT_FOUND)
        return -ENOENT;
    if (num == PREFIX_IS_NOT_DIR)
        return -ENOTDIR;
    if (check_X_in_path(path, 1) == 0)
        return -EACCES;

    cout << "CHOWN: " << fuse_get_context()->uid << " " << fuse_get_context()->gid << endl; 
    if (fuse_get_context()->uid != 0 && fuse_get_context()->gid != 0)
        return -EPERM;  // в документации сложнее... но я буду считать, что измнение владельца/группы можно только привелегированному процессу
    
    INODE *inode = TMPFS_DATA->inodes[num];
    if (uid != (uid_t) -1)
        inode->uid = uid;
    if (gid != (gid_t)-1)  // если значение не -1, то меняем пользвотеля
        inode->gid = gid;
    return 0;
}


// Функция удаляем пользовательские данные - которые в fuse_getcontext()->private_data были
void tmpfs_destroy(void *userdata) {
    delete ((TableInodes *) userdata);
}


// === Структура, которая передаётся во FUSE - тут описаны все функции, необходимые файловой системе: ===
struct fuse_operations tmpfs_oper = {
  .getattr = tmpfs_getattr,
  .readlink = NULL,
  .getdir = NULL,  // устарело
  .mknod = tmpfs_mknod,
  .mkdir = tmpfs_mkdir,
  .unlink = tmpfs_unlink,
  .rmdir = tmpfs_rmdir,
  .symlink = NULL,
  .rename = tmpfs_rename,
  .link = tmpfs_link,
  .chmod = tmpfs_chmod,
  .chown = tmpfs_chown,
  .truncate = tmpfs_truncate,
  //.utime = NULL,  // уже устарело (deprecated) -> вместо этого следующая функция:
  .open = tmpfs_open,
  .read = tmpfs_pread,
  .write = tmpfs_pwrite,
  .statfs = NULL,
  .flush = tmpfs_close,
  .release = NULL,
  .fsync = NULL,
  
  .opendir = tmpfs_opendir,
  .readdir = tmpfs_readdir,
  .releasedir = tmpfs_closedir,
  .fsyncdir = NULL,
  .init = NULL,  // эта функция вызывается в самом начале - перед монированием нашей ФС - и должна возвращать то, что потом попадёт в fuse_getcontext()->private_data
                 // (данные, которые мы можем вытащить в любом месте программы - у нас такие данные - это tmpfs_data - указатель на TableInode, мы используем эти данные, чтобы добавлять/удалять inode)
                 // - но мы и так заполяем private_data, когда вызываем fuse_main, поэтому в init нет необходимости... хотя её можно вызвать и она перезапишет эти данные на те, которые тут вернутся
  .destroy = tmpfs_destroy,  // эта функция вызываеся в самом конце и очищает данные
  .access = NULL,
  .ftruncate = NULL,
  .fgetattr = NULL,
  .lock = NULL,
  .utimens = tmpfs_utimens, 
  .bmap = NULL
  // далее идут спец флаги:
  // flag_nopath = 1 - значит, что для read, write и тд операций нет необходимости вычислять путь path (в какой файл читать/писать мы узнаем через сохранённый номер inode: fi->fh - так и делаем в коде)
  // flag_utime_omit_ok = 1 - принимаем значения UTIME _NOW и _OMIT
};

int main(int argc, char *argv[]) {
    int fuse_stat;

    // проверяем, что монтирование нашей временной fuse-файловой системы производится не с root-правами
    // (то есть что id пользователя не 0), так как использование root приводит к огромным дырам в безопасности!
    if ((getuid() == 0) || (geteuid() == 0)) {
    	fprintf(stderr, "Запуск fuse-приложения из-под root небезопасен!\n");
    	return 1;
    }

    // выводим используемую версию fuse:
    fprintf(stderr, "Используем Fuse версию: %d.%d\n", FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);
    
    // проверяем входные параметры:
    if ((argc < 2) || (argv[argc-1][0] == '-')) {
        fprintf(stderr, "Ошибка параметров, верное использование: ./tmpfs [FUSE и mount опции] mountPoint\n");
        return 1;
    }

    TableInodes *tmpfs_data = new TableInodes;  // создаём струткуру для хранения всех inode 
    if (tmpfs_data == NULL) {
	    perror("Ошибка основного malloc");
        return 1;
    }
   
    // Передаём управление FUSE:
    fprintf(stderr, "about to call fuse_main\n");
    fuse_stat = fuse_main(argc, argv, &tmpfs_oper, tmpfs_data);  // эта функция полностью оперирует файловой системе, вызывает функции, определенные в tmpds_oper, для команд над файловой системе
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}