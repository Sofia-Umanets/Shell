#include <stdbool.h>

#include <errno.h> // для работы с системными ошибками
#include <sys/wait.h> // для работы с системными вызовами wait()
#include <stdio.h>
#include <stdlib.h>   // для работы с памятью, процессами
#include <string.h>
#include <unistd.h>   // системные вызовы POSIX
#include <sys/types.h> // Определения различных типов данных
#include <signal.h>   // для работы с сигналами UNIX
#include <fcntl.h>    // для работы с файловыми дескрипторами
#include <sys/mount.h> //для монтирования файловых систем
#include <sys/stat.h>  // для работы с информацией о файлах
#include <sys/ptrace.h> // для трассировки процессов

#define MAX_LINE 1024    // Максимальная длина строки
#define MAX_HISTORY 100  // Максимальное количество команд в истории

#include <pwd.h>

void execute_partitions3(const char *command) {
    // Разбиваем строку команды на токены
    char *token = strtok((char *)command, " ");
    if (token && strcmp(token, "\\k") == 0) { // Проверяем, что команда начинается с "\k"
        token = strtok(NULL, " "); // Ищем имя устройства или его префикс
        if (token) {
            FILE *fp = fopen("/proc/partitions", "r"); // Открываем файл /proc/partitions
            if (!fp) {
                perror("Не удалось открыть /proc/partitions");
                return;
            }

            printf("Список разделов для устройства \"%s\":\n", token);
            printf("========================================\n");

            char buffer[256];
            int found = 0;

            // Читаем файл /proc/partitions построчно
            while (fgets(buffer, sizeof(buffer), fp)) {
                // Пропускаем строку с заголовками
                if (strncmp(buffer, "major", 5) == 0) {
                    continue;
                }

                // Разбираем строку на части
                int major, minor, blocks;
                char name[32];
                if (sscanf(buffer, "%d %d %d %s", &major, &minor, &blocks, name) == 4) {
                    // Проверяем, начинается ли имя раздела с указанного префикса
                    if (strncmp(name, token, strlen(token)) == 0) {
                        // Выводим основную информацию из /proc/partitions
                        printf("Устройство: %s\n", name);
                        printf("Размер: %d блоков\n", blocks);

                        // Формируем команду для получения более подробной информации через lsblk
                        char lsblk_cmd[256];
                        snprintf(lsblk_cmd, sizeof(lsblk_cmd), "lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT /dev/%s", name);

                        // Выполняем команду lsblk и читаем её вывод
                        FILE *lsblk_fp = popen(lsblk_cmd, "r");
                        if (lsblk_fp) {
                            char lsblk_output[256];
                            while (fgets(lsblk_output, sizeof(lsblk_output), lsblk_fp)) {
                                printf("%s", lsblk_output);
                            }
                            pclose(lsblk_fp);
                        } else {
                            printf("Не удалось получить дополнительную информацию с помощью lsblk\n");
                        }

                        printf("\n");
                        found = 1;
                    }
                }
            }

            fclose(fp); // Закрываем файл

            if (!found) {
                printf("Устройство \"%s\" не найдено\n", token);
            }
            } else {
            printf("Не указано имя устройства или его префикс\n");
        }
    } else {
        printf("Некорректная команда\n");
    }
}


bool execute_partitions(const char *command) {
    char cmd[256];
    // Создаём копию команды, чтобы избежать изменения оригинальной строки
    strncpy(cmd, command, sizeof(cmd));
    cmd[sizeof(cmd) - 1] = '\0';

    // Разбиваем команду на токены
    char *token = strtok(cmd, " ");
    if (token && strcmp(token, "\\l") == 0) {
        token = strtok(NULL, " "); // Получаем имя устройства (например, "sda")
        if (token) {
            // Убираем лишние пробелы в начале имени устройства (если есть)
            while (*token == ' ') {
                token++;
            }

            // Формируем полный путь к устройству
            const char *root = "/dev/";
            char full_path[128];
            snprintf(full_path, sizeof(full_path), "%s%s", root, token);

            // Пытаемся открыть файл устройства
            FILE *device_file = fopen(full_path, "rb");
            if (device_file == NULL) {
                printf("Указанный диск не найден: %s\n", full_path);
                return false; // Устройство не найдено
            }

            // Позиционируемся на 510-й байт (предпоследний байт загрузочной записи)
            int position = 510;
            if (fseek(device_file, position, SEEK_SET) != 0) {
                printf("Ошибка при выполнении операции SEEK!\n");
                fclose(device_file);
                return false;
            }

            // Читаем 2 байта (сигнатура загрузочного сектора)
            uint8_t data[2];
            if (fread(data, 1, 2, device_file) != 2) {
                printf("Ошибка при чтении данных с устройства!\n");
                fclose(device_file);
                return false;
            }
            fclose(device_file);

            // Проверяем сигнатуру загрузочного сектора (0x55AA)
            if (data[1] == 0xAA && data[0] == 0x55) {
                return true; // Устройство загрузочно
            } else {
                printf("Указанный диск не является загрузочным.\n");
                return false; // Устройство не загрузочно
            }
        } else {
            printf("После команды \\l не указано устройство!\n");
            return false;
        }
    } else {
        printf("Неверная команда: %s\n", command);
        return false;
    }
}


void print_user() {
uid_t uid = getuid(); // Получаем ID текущего пользователя
    struct passwd *pw = getpwuid(uid); // Получаем структуру с информацией о пользователе
    if (pw) {
        printf("Текущий пользователь: %s\n", pw->pw_name); // Выводим имя пользователя
    }
}


// Структура для хранения истории команд
typedef struct {
    char commands[MAX_HISTORY][MAX_LINE];  // Массив команд
    int count;  // Счетчик команд
} History;

// Инициализация истории
void init_history(History *history) {
    history->count = 0;  // Обнуляем счетчик при инициализации
}

// Добавление команды в историю
void add_to_history(History *history, const char *command) {
    if (history->count < MAX_HISTORY) {  // Проверяем, не превышен ли лимит истории
        strncpy(history->commands[history->count], command, MAX_LINE);  // Копируем команду в массив
        history->count++;  // Увеличиваем счетчик команд
    }
}

// Сохранение истории в файл
void save_history(History *history, const char *filename) {
    FILE *file = fopen(filename, "w");  // Открываем файл для записи
    if (file) {
        for (int i = 0; i < history->count; i++) {
            fprintf(file, "%s\n", history->commands[i]);  // Записываем каждую команду в файл
        }
        fclose(file);  // Закрываем файл
    }
}

// Загрузка истории из файла
void load_history(History *history, const char *filename) {
    FILE *file = fopen(filename, "r");  // Открываем файл для чтения
    if (file) {
        char line[MAX_LINE];
        int i = 0;
        while (fgets(line, MAX_LINE, file) && i < MAX_HISTORY) {  // Читаем строки из файла
            strncpy(history->commands[i], line, MAX_LINE);  // Копируем строку в массив
            history->commands[i][strlen(line) - 1] = '\0';  // Удаляем символ новой строки
            i++;
	}
        history->count = i;  // Обновляем счетчик команд в истории
        fclose(file);  // Закрываем файл
    }
}

// Выполнение команды echo
void execute_echo(const char *command) {
    char *token = strtok((char *)command, " ");
    if (token && strcmp(token, "echo") == 0) {
        token = strtok(NULL, " ");  // Получаем следующий токен
        while (token) {
            printf("%s ", token);  // Выводим токен
            token = strtok(NULL, " ");  // Получаем следующий токен
        }
        printf("\n");  // Выводим новую строку после завершения вывода всех токенов
    }
}

// Выполнение команды для вывода переменной окружения
void execute_env(const char *command) {
    char *token = strtok((char *)command, " ");
    if (token && strcmp(token, "\\e") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            // Убираем символ '$' из строки
            if (token[0] == '$') {
                token++;
            }
            char *value = getenv(token);  // Получаем значение переменной окружения
            if (value) {
                printf("%s\n", value);  // Выводим значение переменной, если оно найдено
            } else {
                printf("Переменная окружения не найдена\n");  // Сообщаем, что переменная не найдена
            }
        }
    }
}

// Выполнение команды для запуска бинарника
void execute_binary(const char *command) {
    char *token = strtok((char *)command, " ");
    if (token && strcmp(token, "\\b") == 0) {
        token = strtok(NULL, " ");
        if (token) {                            // Проверяем, что имя программы предоставлено
            pid_t pid = fork();                 // Создаем новый процесс
            if (pid == 0) {                     // Если это процесс-потомок
                // Ребенок
                execlp(token, token, NULL);     // Заменяем образ процесса новой программой
                perror("execlp");               // Выводим сообщение об ошибке, если execlp не выполнилось
                exit(1);                        // Завершаем процесс с кодом ошибки
            } else if (pid > 0) {
                // Родитель
                int status;
                waitpid(pid, &status, 0);       // Ожидаем завершения дочернего процесса
            } else {
                perror("fork");
// Выводим сообщение об ошибке, если fork не удался
            }
        }
    }
}

void sighup() {
    signal(SIGHUP, sighup);                     // Переустанавливаем обработчик сигнала
    printf("Configuration reloaded\n");         // Выводим сообщение о перезагрузке конфигурации
fflush(stdout);
}


void execute_cron(const char *command) {
    char *token = strtok((char *)command, " ");
    if (token && strcmp(token, "\\cron") == 0) {
        FILE *pipe = popen("crontab -l", "r");//открывается канал к команде crontab -l и возвращает указатель на файловый поток.
        if (pipe == NULL) {
            perror("Ошибка открытия канала");
            return;
        }

        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            fputs(buffer, stdout);
        }

        pclose(pipe);
    }
}



void execute_mem(const char *command) {
    char *token = strtok((char *)command, " ");
    if (token && strcmp(token, "\\mem") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            pid_t pid = atoi(token);            // Преобразуем строку в число, представляющее PID
            if (pid > 0) {                      // Проверяем, что PID корректен (больше нуля)
                errno = 0;
                long ptrace_result = ptrace(PTRACE_ATTACH, pid, NULL, NULL); // Присоединяемся к процессу для его трассировки
                if (ptrace_result == 0) {       // Если трассировка успешна
                    int status;
                    waitpid(pid, &status, 0);   // Ожидаем изменения состояния трассируемого процесса
                    char maps_path[256];
                    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid); // Формируем путь к файлу карты памяти процесса
                    FILE *maps_file = fopen(maps_path, "r"); // Открываем файл карты памяти
                    if (maps_file) {
                        char line[256];
                        printf("Memory Regions:\n");        // Выводим заголовок для списка регионов памяти
                        while (fgets(line, sizeof(line), maps_file)) { // Читаем строки из файла
                            printf("%s", line);            // Выводим информацию о регионе памяти
                        }
                        fclose(maps_file);                // Закрываем файл
                    } else {
                        perror("Не удалось открыть maps файл"); // Выводим сообщение об ошибке, если файл не удалось открыть
                    }
                    ptrace(PTRACE_DETACH, pid, NULL, NULL); // Отсоединяемся от трассируемого процесса
                } else {
                    switch(errno) {
                        case EPERM:                        // Если ошибка связана с недостаточными правами
                            printf("Недостаточно прав. Запустите программу с sudo.\n");
                            break;
                        case ESRCH:                        // Если процесс не найден
                            printf("Процесс не существует.\n");
                            break;
                        default:
                            perror("Ошибка при попытке отслеживания процесса"); // Вывод других ошибок
                    }
                }
            } else {
                printf("Неверный PID\n");       // Сообщаем пользователю, что предоставлен неверный PID
            }
        }
    }
}

// Обработка команд
void process_command(const char *command, History *history) {
    if (strcmp(command, "exit") == 0 || strcmp(command, "\\q") == 0) {
        // Если команда "exit" или "\\q", сохраняем историю и выходим
        save_history(history, "history.txt");
        exit(0);
    } else if (strncmp(command, "echo", 4) == 0) {
        execute_echo(command);
    } else if (strncmp(command, "\\e", 2) == 0) {
        execute_env(command);
    } else if (strncmp(command, "\\b", 2) == 0) {
        execute_binary(command);
    } else if (strncmp(command, "\\l", 2) == 0) {
        // Проверяем, загрузочно ли устройство
        if (execute_partitions(command)) {
            printf("Указанный диск загрузочный.\n");
        } else {
            printf("Указанный диск не является загрузочным или его не существует\n");
        }
    }else if (strncmp(command, "\\k", 2) == 0) {
	execute_partitions3(command);
    } else if (strncmp(command, "\\cron", 5) == 0) {
        execute_cron(command);
    } else if (strncmp(command, "\\mem", 4) == 0) {
        execute_mem(command);
    } else {
        printf("Не команда: %s\n", command);
    }
}

void shell_loop(History *history) {
    char line[MAX_LINE];
    while (1) {
        signal(SIGHUP, sighup);  // Устанавливаем обработчик сигнала SIGHUP
        printf("$ ");            // Выводим приглашение к вводу
        if (fgets(line, MAX_LINE, stdin) == NULL) {  // Читаем строку из stdin
            if (feof(stdin)) {
                printf("\n");   // Если достигнут конец файла, выходим из цикла
                break;
            } else {
                perror("Ошибка чтения");  // Выводим сообщение об ошибке
                exit(1);
            }
        }

        line[strcspn(line, "\n")] = '\0';  // Удаляем символ новой строки из ввода
        add_to_history(history, line);     // Добавляем команду в историю
        process_command(line, history);    // Обрабатываем команду
    }
}


int main() {
    pid_t pid = getpid();
    printf("PID: %d\n", pid);
    print_user();
    History history;
    init_history(&history);  // Инициализируем историю команд
    load_history(&history, "history.txt");  // Загружаем историю из файла
    shell_loop(&history);  // Запускаем основной цикл оболочки
    return 0;
}







