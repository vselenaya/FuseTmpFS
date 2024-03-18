# Реализация временной файловой системы с помощью FUSE

### Описание:
Данный проект реализует файловую систему для UNIX—систем, которая хранит всю информацию в оперативной памяти, выделяемой динамически. Такая файловая система работает в пользовательском пространстве, а потому доступна для монтирования пользователям без привелегий. Это достигается с помощью механизама FUSE.

### Полезные материалы:

Документация по FUSE: https://libfuse.github.io/doxygen

Пример другого проекта, реализованного для FUSE: https://www.cs.nmsu.edu/~pfeiffer/fuse—tutorial

### Использование:

1) Компиляция кода производится с помощью `Makefile`:
```bash
make
```
После этого появится исполняемый файл `tm`.

2) Далее необходимо запустить код, указав папку, куда монтировать файлову систему. Также можно указать ключи.
Пример запуска:
```bash
./tm —d mnt
```
здесь ключ `—d` показывает, что код необходимо запускать в открытом терминале, чтобы он не отсоединялся от него, а продолжал писать отладочный вывод.

### Теория:
Здесь будет описана теория, что вообще за FUSE и как с ним работать.

Для начала, что вообще такое `файловая система`? Согласно Википедии: Фа́йловая систе́ма (англ. file system) — порядок, определяющий способ организации, хранения и именования данных. Проще говоря, файловая система — это папки (директории), файлы и удобное их расположение; это иерархическая структура хранения данных.
 
Здесь и далее будем говорить о файловых системах Linux.

Файловых систем может быть огромное множество: файлы хранятся на жестком диске компьютера или на флешке, некоторые данные можно организовать в виде файлов (так, например, делают `procfs` и `sysfs`, которые собирают данные о работет компьютера в виде файлов), файлами могут быть даже сокеты для работы по сети и тд. Со всеми этими файловыми системами хочется работать одинаково: если хочется вывести файл, например, то просто запустить команду `cat` и не важно, сокет это, реальный файл и тд... чтобы так было, существует `VFS` — виртуальная файловая система, которая является некоторой прослойкой между ядром и конкретной файловой системой. 

Работает это так: когда пользователь выполняет запрос к файловой системе — например, делает команду `ls -l`  — этот запрос отправляется в `VFS`, которая определяет, какой реальной файловой системе этот запрос относится, и в зависимости от этого передаёт команду далее: например, если эту команду пользователь выполнил на жёстком диске, то VFS делигирует эту команду драйверу жёсткого диска — и уже его заботой будет пойти в жёсткий диск, считать нужные байты из него, учитывая при этом производителя, файловую систему на нём: FAT32, NFTS, и тд. Если команда запускалась в `procfs`, то `VFS` сделает запрос ядру операционной системы, которое должно будет собрать сведения о работе и выдать их в виде файлов... Так или иначе, `VFS` позволяет работать со всеми файловыми системами стандартизированно, не заставляя учить миллион команд под FAT32, NTFS, EXT4, ...

Как же добавляется новая файловая система? Это делается с помощью `монтирования`: этот процесс должен добавить в общее пространство имён новую файловую систему (то есть она будет отображаться как обычные файлики и папочки в графическом интерфейсе и в комнадной строке, можно будет по ней проходить через обычные `cd`, `ls`, ...), а также отождествить подходящий драйвер с новой файловой системой — именно к этому драйверу будет обращаться `VFS`, когда будут поступать запросы к этой файловой системе. Например, когда мы вставляем флешку в компьютер, она монтируется: содержимое, струтктуры папок и тд — вся файловая система флешки отображается на компьютере как обычно; мы можем копировать файлы из неё, можем их перемещать, можем выводить и тд — всё это возможно, так как при монтировании `VFS` поняла, что все запросы на эту файловую систему нужно адресовать драйверу флешки.

В чём сложность такого подхода? В том, что для каждой файловой системы нужно писать свой драйвер, который при этом работает в ядре, а значит недоступен непривелегированным пользователям. Решением стал механизм `FUSE` (filesystem in userspace) — дословно: «файловая система в пользовательском пространстве».

Как работает `FUSE`? В ядре Linux находится специальный драйвер Fuse, который обеспечивает всю работоспособность. А в пользовательском пространстве работает обычная программка (например, та самая, котороя в данном проекте), задача которой реализовать все необходимые функции файловой системы. Далее, когда мы создали и подмонтировали файловую систему на основе FUSE, мы снова работем с этой файловой системой, как с обычной. Тогда при запросе к этой файловой системе, VFS отправит этот запрос драйверу FUSE, а он в свою очередь отправит этот запрос в пользовательское пространство — той самой программке, которая должна обработать этот запрос и вернуть ответ обратно драйверу FUSE, который передаст его в VFS, а уже та отобразит результат пользователю. В итоге создание своей файловой системы сводится к написанию лишь одной программы в пользовательском пространстве, которая должна реализовать необходимые функции для работы файловой системы: например, необходимы функции `pread`, `pwrite` для чтения и записи файлов... И так как программка работает в пользовательском пространстве, для её монтирования не нужны права суперпользователя!

Хорошим примером использования механизма `FUSE` является `sshfs`. Это реализация файловой системы, которая монтирует удалённую папку через ssh. А именно: мы подключаемся к удалённому компьютеру через ssh, далее мы можем взять папку на этом удалённом компьютере и подмонтировать её себе на локальный компьютер - эта папочка будет отображаться как обычная локальная директория, её содержимое будет доступно как будто локально, хотя сама информация находится на удалённом компьютере! В эту папку можно перемещать файлы (и они появятся на удалённом компьютере), из неё можно копировать файлы себе в другие папки (и тогда они скачаются с удалённого компьютера...).\
Как же это работает? А очень просто: та самая программка в пользовательском пространстве при поступлении запроса, делает этот запрос удалённо: по сети через ssh переводит этот запрос на удалённый компьютер, где он выполянется в реальной папке. Поэтому, например, когда мы делаем `ls -l` в папке локально, программка через ssh выполняет этот запрос на удалённом компьютере, а результат выдаёт обратно. Получается, что пользователь видит файлы и содержимое удалённой папки как будто локально у себя на компьютере, хотя никакие данные не скачивались (это было бы очень долго)! А при копировании файлов или команде `cat`, например, эта программка получит содержимое файле из удалённого компьютера и отобразит нам. Таким образом можно, например, удобно скачивать/загружать файлы с сервера: просто делаем папку с sshfs, а затем копируем/вставляем в неё файлы.

Ну что ж, осталось разобраться, как же написать программу в пользовательском пространстве, чтобы она выполняла нужные запросы к файлвой системе. Глобально такой программе нужно следующее:
1. Реализовать функции из структуры `fuse_operations`:
```C
struct fuse_operations {
	int (*getattr) (const char *, struct stat *);
	int (*readlink) (const char *, char *, size_t);
	int (*getdir) (const char *, fuse_dirh_t, fuse_dirfil_t);

	int (*mknod) (const char *, mode_t, dev_t);
	int (*mkdir) (const char *, mode_t);
	int (*unlink) (const char *);
	int (*rmdir) (const char *);
	int (*symlink) (const char *, const char *);
	int (*rename) (const char *, const char *);
	int (*link) (const char *, const char *);
	int (*chmod) (const char *, mode_t);
	int (*chown) (const char *, uid_t, gid_t);
	int (*truncate) (const char *, off_t);

	int (*utime) (const char *, struct utimbuf *);

	int (*open) (const char *, struct fuse_file_info *);
	int (*read) (const char *, char *, size_t, off_t, struct fuse_file_info *);
	int (*write) (const char *, const char *, size_t, off_t, struct fuse_file_info *);

	int (*statfs) (const char *, struct statvfs *);
	int (*flush) (const char *, struct fuse_file_info *);
	int (*release) (const char *, struct fuse_file_info *);
	int (*fsync) (const char *, int, struct fuse_file_info *);

	int (*setxattr) (const char *, const char *, const char *, size_t, int);
	int (*getxattr) (const char *, const char *, char *, size_t);
	int (*listxattr) (const char *, char *, size_t);
	int (*removexattr) (const char *, const char *);

	int (*opendir) (const char *, struct fuse_file_info *);
	int (*readdir) (const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info *);
	int (*releasedir) (const char *, struct fuse_file_info *);
	int (*fsyncdir) (const char *, int, struct fuse_file_info *);

	void *(*init) (struct fuse_conn_info *conn);
	void (*destroy) (void *);

	int (*access) (const char *, int);
	int (*create) (const char *, mode_t, struct fuse_file_info *);
	int (*ftruncate) (const char *, off_t, struct fuse_file_info *);
	int (*fgetattr) (const char *, struct stat *, struct fuse_file_info *);
	int (*lock) (const char *, struct fuse_file_info *, int cmd, struct flock *);
	int (*utimens) (const char *, const struct timespec tv[2]);
	int (*bmap) (const char *, size_t blocksize, uint64_t *idx);

	unsigned int flag_nullpath_ok:1;
	unsigned int flag_nopath:1;
	unsigned int flag_utime_omit_ok:1;
	unsigned int flag_reserved:29;

	int (*ioctl) (const char *, int cmd, void *arg, struct fuse_file_info *, unsigned int flags, void *data);
	int (*poll) (const char *, struct fuse_file_info *, struct fuse_pollhandle *ph, unsigned *reventsp);
	int (*write_buf) (const char *, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *);
	int (*read_buf) (const char *, struct fuse_bufvec **bufp, size_t size, off_t off, struct fuse_file_info *);
	int (*flock) (const char *, struct fuse_file_info *, int op);
	int (*fallocate) (const char *, int, off_t, off_t, struct fuse_file_info *);
};
```
именно эти функции будут вызываться драйвером FUSE при поступлении тех или иных запросов к файловой системе.\
Например, при вызове команды `ls -l` понадобятся функции `getattr` (для получения данных содержимого: время создания, уровень доступа и тд... в обычной файловой системе это делает функция stat:  см. `man 2 stat`) и `readdir` (функция для чтения содержимого директории); для команды `touch` понадобится функция `mknod`; для команды `cat` понадобится функция `read` для чтения файла.\
Разумеется, совершенно необязательно реализовывать все функции этой структуры — можно реализовать только часть, просто тогда часть обычного функционала файловых систем работать не будет. Например, можно не реализовывать права доступа и функции `chown` и `chmod`, тогда у такой файловой системы не будет безопасноти и разграничений доступа к файлам и директориям...

2. Необходимо создать эту структуру и заполнить её поля: то есть передать указатели на реализованные в программе функции в нужные поля структуры, нереализованные функции можно заполнить NULL. Например, кусок моей программы выглядит так:
```C
struct fuse_operations tmpfs_oper = {
  .getattr = tmpfs_getattr,
  .readlink = NULL,
  ...
}
```
Здесь я создаю структуру tmpfs_oper типа fuse_operations, а также передаю указатели на реализованные в моей программе функции. Например, в моей программе функция получения инфорации называется `tmpfs_getattr` - её передаю в нужно поле структуры, а вот функцию readlink я не реализовал - передаю NULL.

3. Запустить функцию `fuse_main`:
```C
fuse_stat = fuse_main(argc, argv, &tmpfs_oper, tmpfs_data);
```
* `argc`, `argv` - параметры запуска, в частности       название папки (она должна быть пустой) в обычной файловой системе - именно туда будет подмонтирована наша fuse-файловая система (эта папка станет корнем).
* `&tmpfs_oper` - указатель на структуру с реализованными нами операциями
* `tmpfs_data` - указатель на общие данные, которые мы можем в любой момент (в любой своей функции использовать) достать через `fuse_get_context()->private_data`.
  
Эта функция как бы скажет драйверу FUSE, какие именно функции использовать при работе с файловой системой.

4. Важное дополнение: почти все функции в случае успешной работы возвращают число 0. А вот в случае ошибки они должны вернуть отрицательное число: минус код ошибки (-errno). Код ошибки удобнее всего искать в `man 2 ` для каждой функции отедльно.

### Реализация:
Данный проект представляет собой реализацию файловой системы, которая целиком и полностью хранится в оперативной памяти, то есть все файлы, папки и тд, которые содержатся в этой файловой системе, находятся в оперативной памяти.

Для реализации я использую две основных структуры:
1. Для хранения Inode - единицы в файловой системе (каждый файл или папка представляет собой inode с опреденными данными):
```C
struct INODE {
    int num;  // номер, идентификатор inode
    void *data;  // указатель на данные - байты файла или список файлов каталога
    INODE *par;  // указатель на родительскую директорию (NULL для корневой)
    mode_t mode;  // права доступа и тип inode
    nlink_t nlink;
    uid_t uid;  // владелец
    gid_t gid;
    int opened_by; 

    struct timespec st_atim;  // время доступа
    struct timespec st_mtim;
    struct timespec st_ctim; 
}
```

2. Для хранения всей файловой системы используется структура, в которой есть вектор всех inode:
```C
struct TableInodes {
    vector <INODE*> inodes;  // указателе на все inode в данной файловой систем
    vector <int> free_inodes;  // идентификаторы (= индексы в векторе) тех inode, которые пока не участвую в файловой системе
    size_t N;
}
```


\
Данная реализация файловой системы поддерживает станадартные операции: чтения директории, создание файла/директории, работа с файлами: чтение и запись, жёсткие ссыли. Также поддерживается время доступа к файлу, время его модификации.\
Поддерживается контроль прав доступа (на чтение, запись, исполнение), изменение доступа (chmod), изменение владельца (chown). Поэтому в принципе можно открывать многопользовательский доступ, однако гарантий, что что-то не упущено и всё действительно безопасно - нет.

Если есть желание открыть многопользовательский доступ, необходимо проверить, что в файле конфигурации FUSE `/etc/fuse.conf` строчка `user_allow_other` раскоментирована - иначе драйвер FUSE просто не даст пользоваться этой файловой системой никому, кроме того, кто изначально запустил программу и подмонтировал её. Даже суперпользователю (root) не удастся пользоваться ею: например, команда `sudo chown user file` выдаст ошибку при запуске в этой файловой системе, так как запускается от суперпользователя (запуск с sudo). То есть файл должен выглядеть например так:
```bash
vselenaya@computer:~$ cat /etc/fuse.conf
# The file /etc/fuse.conf allows for the following parameters:

user_allow_other
```
Правда одного этого файла ещё не достаточно, необходимо запускать с ключом `-o allow_other`, чтобы был доступ для других пользователей.

Кстати, с многопользовательским доступом связана ещё одна особенность реализации: когда в коде устанавливается владелец файла, для его получения вызывается функция `fuse_get_context()->uid`. Важно использовать именно этот вариант, ведь он получает uid именно того, кто сделал запрос к файловой системе (пользователю, который её использует), а не просто `getuid()`, так как он возвращает uid процесса драйвера FUSE - то есть того, кто монтировал файловую систему.

### Пример использования:
Для начала создадим в рабочей папке проекта (там, где запускается программка) пустую папку:
```console
vselenaya@computer:~/hw_fuse/tmpfs$ mkdir mnt
```
Далее монтируем в эту папку нашу файловую систему - для этого просто запускаем программу с указанием созданной папки `mnt`. Она в дальнейшем будет вместилищем всей нашей файловой системы, она будет её корнем:
```console
vselenaya@computer:~/hw_fuse/tmpfs$ ./tm -d -s -o allow_other mnt
```
(ключ `-d` оставляет процесс запущенным в этом окне терминала, поэтому выводится отладочная информация..., ключ `-s` выключает многопоточность (так как в данном проекте многопоточность не учитывалась - нет защиты от гонки потоков - лучше её выключить), `-o allow_other` включает многопользовательский доступ)

Далее можем перейти в каталог `mnt`, с этого момента мы находимся в нашей файловой системе; все операции тут - выполняет код из данного проекта:
```console
vselenaya@computer:~/hw_fuse/tmpfs$ cd mnt/
vselenaya@computer:~/hw_fuse/tmpfs/mnt$
```

Можно позапускать команды и проверить, как работает программа:
1. Изначально файловая система пустая:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l
итого 0
```
2. Далее можно создать вложенные директории:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ mkdir -p 1/2/3/4/5
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l
итого 0
drwxrwxr-x 3 vselenaya vselenaya 3 мар 19 00:25 1
```
3. Также создадим файл:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ touch file
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l
итого 0
drwxrwxr-x 3 vselenaya vselenaya 3 мар 19 00:25 1
-rw-rw-r-- 1 vselenaya vselenaya 0 мар 19 00:31 file
```

4. Перенесём директорию 3 в корень:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ mv 1/2/3 .
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l
итого 0
drwxrwxr-x 3 vselenaya vselenaya 3 мар 19 00:25 1
drwxrwxr-x 3 vselenaya vselenaya 3 мар 19 00:25 3
-rw-rw-r-- 1 vselenaya vselenaya 0 мар 19 00:31 file
```

5. Проверим чтение и запись в файл:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ echo "123456789" > file
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ cat file
123456789
```

6. Проверим права доступа у директории. Как известно, бит `x` у директории даёт возможность что-либо делать с её содержимым. Без этого бита с содержимым директории ничего не сделать - ни получить о нём данные, ни создать что-нибудь... хотя узнать, какие есть файлы в директории - можно, но вот их атрибуты и тд - нет. Проверяем:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ chmod -x 1
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l
итого 0
drw-rw-r-- 3 vselenaya vselenaya  3 мар 19 00:25 1
drwxrwxr-x 3 vselenaya vselenaya  3 мар 19 00:25 3
-rw-rw-r-- 1 vselenaya vselenaya 10 мар 19 00:33 file
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l 1
ls: невозможно получить доступ к '1/2': Отказано в доступе
итого 0
?????????? ? ? ? ?            ? 2
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ cd 1
vselenaya@computer:~/hw_fuse/tmpfs/mnt/1$ ls -l
ls: невозможно получить доступ к '2': Отказано в доступе
итого 0
?????????? ? ? ? ?            ? 2
vselenaya@computer:~/hw_fuse/tmpfs/mnt/1$ cd 2
bash: cd: 2: Отказано в доступе
```

7. Вернём бит `x` и заработает:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt/1$ cd ..
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ chmod +x 1
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l 1
итого 0
drwxrwxr-x 3 vselenaya vselenaya 2 мар 19 00:32 2
```

8. Уберём право на чтение у файла. Читать нельзя, а писать можно:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ chmod -r file 
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l
итого 0
drwxrwxr-x 3 vselenaya vselenaya  3 мар 19 00:25 1
drwxrwxr-x 3 vselenaya vselenaya  3 мар 19 00:25 3
--w--w---- 1 vselenaya vselenaya 10 мар 19 00:33 file
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ cat file
cat: file: Отказано в доступе
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ echo "123" > file
```

9. Дадим все права:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ chmod +rwx file
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l
итого 0
drwxrwxr-x 3 vselenaya vselenaya 3 мар 19 00:25 1
drwxrwxr-x 3 vselenaya vselenaya 3 мар 19 00:25 3
-rwxrwxr-x 1 vselenaya vselenaya 4 мар 19 00:39 file
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ cat file
123
```

10. Изменим владельца файла на root. Это можно сделать (как и вообще любое имзенение владельца в данном проекте только с правами суперпользователя - от sudo). Владелец и правда меняется:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ chown root file
chown: изменение владельца 'file': Операция не позволена
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ sudo chown root file
[sudo] пароль для vselenaya: 
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l
итого 0
drwxrwxr-x 3 vselenaya vselenaya 3 мар 19 00:25 1
drwxrwxr-x 3 vselenaya vselenaya 3 мар 19 00:25 3
-rwxrwxr-x 1 root      vselenaya 0 мар 19 00:39 file
```

11. Теперь изменить права у файла обычному пользователю уже нельзя, так как владелец поменялся:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ chmod -r file 
chmod: изменение прав доступа для 'file': Операция не позволена
```

12. Но можем спокойно удалить файл, так как у текущего пользователя vselenaya группа совпадает с группой у файла:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ rm file 
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -l
итого 0
drwxrwxr-x 3 vselenaya vselenaya 3 мар 19 00:25 1
drwxrwxr-x 3 vselenaya vselenaya 3 мар 19 00:25 3
```

13. Проверим всякие ошибки:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ mkdir 1
mkdir: невозможно создать каталог «1»: Файл существует
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ cat 2
cat: 2: Нет такого файла или каталога
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ cat 3
cat: 3: Это каталог
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ mv 3/4 3/4/5
mv: невозможно перенести '3/4' в свой собственный подкаталог, '3/4/5/4'
```

14. Можно удалить всё и заново создать файлики:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ rm -r *
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ echo "123" > f1
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ echo "abcdef" > f2
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ mkdir 1 2 3
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ mv 2 1
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ ls -lasi
итого 4
     1 0 drwxrwxrwx 4 vselenaya vselenaya    6 мар 19 01:11 .
794809 4 drwxrwxr-x 4 vselenaya vselenaya 4096 мар 18 23:56 ..
    11 0 drwxrwxr-x 2 vselenaya vselenaya    3 мар 19 01:11 1
    13 0 drwxrwxr-x 2 vselenaya vselenaya    2 мар 19 01:11 3
     9 0 -rw-rw-r-- 1 vselenaya vselenaya    4 мар 19 01:11 f1
    10 0 -rw-rw-r-- 1 vselenaya vselenaya    7 мар 19 01:11 f2
```
Число слева - это номер inode в нашей файловой системе. Только у `..` странный номер, так как `..` ссылается в родительскую директорию mnt - а это уже вне нашей файловой системы, там другие номера inode.

15. Можем выйти из файловой системы и размонтировать её. В этот момент программа `tm` завершится, а всё, что было в файловой системе - сотрётся, так как было в оперативной памят:
```console
vselenaya@computer:~/hw_fuse/tmpfs/mnt$ cd ..
vselenaya@computer:~/hw_fuse/tmpfs$ umount mnt
```

16. Если хочется узнать больше о способах и ключах запуска, можно запустить `--help`:

```console
vselenaya@computer:~/hw_fuse/tmpfs$ ./tm --help mnt
Используем Fuse версию: 2.9
about to call fuse_main
usage: ./tm mountpoint [options]

general options:
    -o opt,[opt...]        mount options
    -h   --help            print help
    -V   --version         print version

FUSE options:
    -d   -o debug          enable debug output (implies -f)
    -f                     foreground operation
    -s                     disable multi-threaded operation

    -o allow_other         allow access to other users
    -o allow_root          allow access to root
    -o auto_unmount        auto unmount on process termination
    -o nonempty            allow mounts over non-empty file/dir
    -o default_permissions enable permission checking by kernel
    -o fsname=NAME         set filesystem name
    -o subtype=NAME        set filesystem type
    -o large_read          issue large read requests (2.4 only)
    -o max_read=N          set maximum size of read requests

    -o hard_remove         immediate removal (don't hide files)
    -o use_ino             let filesystem set inode numbers
    -o readdir_ino         try to fill in d_ino in readdir
    -o direct_io           use direct I/O
    -o kernel_cache        cache files in kernel
    -o [no]auto_cache      enable caching based on modification times (off)
    -o umask=M             set file permissions (octal)
    -o uid=N               set file owner
    -o gid=N               set file group
    -o entry_timeout=T     cache timeout for names (1.0s)
    -o negative_timeout=T  cache timeout for deleted names (0.0s)
    -o attr_timeout=T      cache timeout for attributes (1.0s)
    -o ac_attr_timeout=T   auto cache timeout for attributes (attr_timeout)
    -o noforget            never forget cached inodes
    -o remember=T          remember cached inodes for T seconds (0s)
    -o nopath              don't supply path if not necessary
    -o intr                allow requests to be interrupted
    -o intr_signal=NUM     signal to send on interrupt (10)
    -o modules=M1[:M2...]  names of modules to push onto filesystem stack

    -o max_write=N         set maximum size of write requests
    -o max_readahead=N     set maximum readahead
    -o max_background=N    set number of maximum background requests
    -o congestion_threshold=N  set kernel's congestion threshold
    -o async_read          perform reads asynchronously (default)
    -o sync_read           perform reads synchronously
    -o atomic_o_trunc      enable atomic open+truncate support
    -o big_writes          enable larger than 4kB writes
    -o no_remote_lock      disable remote file locking
    -o no_remote_flock     disable remote file locking (BSD)
    -o no_remote_posix_lock disable remove file locking (POSIX)
    -o [no_]splice_write   use splice to write to the fuse device
    -o [no_]splice_move    move data while splicing to the fuse device
    -o [no_]splice_read    use splice to read from the fuse device

Module options:

[iconv]
    -o from_code=CHARSET   original encoding of file names (default: UTF-8)
    -o to_code=CHARSET	    new encoding of the file names (default: UTF-8)

[subdir]
    -o subdir=DIR	    prepend this directory to all paths (mandatory)
    -o [no]rellinks	    transform absolute symlinks to relative
```
