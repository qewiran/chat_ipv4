#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// глобальный мьютекс, блокирует вывод из другого потока
pthread_mutex_t io_mutex = PTHREAD_MUTEX_INITIALIZER;

// api posix потоков такое, что функция должна принимать void*
void *listener_thread(void *arg) {
  // кастим аргумент, чтобы получить дескриптор сокета
  int sock = *(int *)arg;

  // recvfrom получит IP адрес отправителя из сокета
  struct sockaddr_in sender_addr;
  socklen_t addr_len = sizeof(sender_addr);
  // +1 символ, чтобы добавить \0 в конец
  char buffer[1001];

  while (1) {
    // recvfrom ожидает получение данных, блокирует выполнение потока во время
    // ожидания
    ssize_t bytes = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr *)&sender_addr, &addr_len);

    if (bytes == -1) {
      perror("recvfrom");
      continue;
    }
    else if (bytes>1000){
      printf("Слишком большое сообщение! (>1000 символов)\n");
      continue;
    }

    // так как получаем в буфер просто символы, а не строку, то ставим
    // терминатор, чтобы работать с символами, как со строкой (выводить)

    buffer[bytes] = '\0';

    // strtok дает указатель на первую строку до указанного разделителя
    // то есть сообщение nick: message делится на
    // char* nickname = "nick"
    // char* msg = "message"
    char *nickname = strtok(buffer, ":");
    // указание NULL продолжает разбор строки с места остановки
    char *msg = strtok(NULL, "\n");
    if (nickname && msg) {
      pthread_mutex_lock(&io_mutex);
      // inet_ntoa переводит адрес отправителя из бинарного формата в строку
      printf("[%s] %s: %s\n\n", inet_ntoa(sender_addr.sin_addr), nickname, msg);
      // вывод без буферизации для уменьшения задержек
      fflush(stdout);
      pthread_mutex_unlock(&io_mutex);
    }
  }
  return NULL;
}

// структура для передачи аргументов в функцию sender_thread (т.к. posix api
// требует void* аргументом)
struct sender_thread_args {
  int sock;
  uint16_t port;
};

void *sender_thread(void *arg) {
  struct sender_thread_args args = *(sender_thread_args *)arg;
  struct sockaddr_in broadcast_addr;

  // +1 символ, чтобы добавить \0 в конец
  char nickname[65];
  char message[1001];

  pthread_mutex_lock(&io_mutex);
  printf("Введите никнейм (до 64 символов): ");
  fflush(stdout);
  fgets(nickname, sizeof(nickname), stdin);
  nickname[strcspn(nickname, "\n")] = '\0';
  pthread_mutex_unlock(&io_mutex);

  // зануляем поля broadcast_addr (sin_zero д. б. 0) для избегания UB
  memset(&broadcast_addr, 0, sizeof(broadcast_addr));
  broadcast_addr.sin_family = AF_INET;
  broadcast_addr.sin_port = htons(args.port);
  inet_pton(AF_INET, "255.255.255.255", &broadcast_addr.sin_addr);

  bool is_sending = true;

  while (is_sending) {
    pthread_mutex_lock(&io_mutex);
    printf("Введите сообщение: ");
    fflush(stdout);
    fgets(message, sizeof(message), stdin);
    printf("\033[A\033[2K"); // чистим строку ввода
    // меняем \n на нуль-терминатор, чтобы не было переноса
    message[strcspn(message, "\n")] = '\0';
    pthread_mutex_unlock(&io_mutex);

    char full_msg[1001];

    // форматируем строку nickname: message
    snprintf(full_msg, sizeof(full_msg), "%s: %s", nickname, message);

    // отправляем UDP-диаграмму с пом. sendto:
    // header: [порт отправителя из сокета args.sock][порт получателя из broadcast_addr.sin_port]
    // [длина данных strlen(full_msg)][контр сумма (не знаю, есть ли)]
    //
    // data: [full_msg] длиной, указанной в заголовке
    //
    sendto(args.sock, full_msg, strlen(full_msg), 0,
           (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  // пытаюсь уменьшить задержку, убирая буферизацию вывода
  setvbuf(stdout, NULL, _IONBF, 0);

  if (argc != 3) {
    fprintf(stderr, "использование: %s <ip> <port>", argv[0]);
    return 1;
  }

  char *ip_addr_string = argv[1];
  char *port_string = argv[2];
  uint16_t port_uint16 = atoi(port_string);

  // открываем сокет, AF_INET - адреса из IPv4, SOCK_DGRAM - семантика передачи
  // UDP-датаграмм для сокета 0 - выбор протокола передачи по умолчанию, для UDP
  // (SOCK_DGRAM) есть только значение IPPROTO_UDP, так что можно было указать
  // его, либо 0, что равносильно
  int sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock == -1) {
    perror("socket");
    return 2;
  }
  // опции для сокета, чтобы использовать один и тот же адрес и порт копиями
  // программы, избегая ошибки address already in use
  int reuse = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) {
    perror("setsockopt");
    close(sock);
    return 2;
  }
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse))) {
    perror("setsockopt");
    close(sock);
    return 2;
  }

  // опция широковещательной передачи (на все устройства без конкретного
  // получателя)
  int broadcast = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast,
                 sizeof(broadcast))) {
    perror("socket");
    return 2;
  }

  // объект для связи сокета и указанного в аргументах консоли адреса и порта
  struct sockaddr_in local_addr;
  memset(&local_addr, 0, sizeof(local_addr));
  local_addr.sin_family = AF_INET;
  // htons - преобразует порт из числа в сетевой порядок байт (бинарный)
  local_addr.sin_port = htons(port_uint16);
  // inet_pton - преобразует ip адрес из строки в бинарный адрес
  inet_pton(AF_INET, ip_addr_string, &local_addr.sin_addr);

  // bind - связывание сокета с портом и адресом из аргументов
  if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr))) {
    perror("bind");
    close(sock);
    return 3;
  }
  printf("Сокет %d слушает %s:%d\n", sock, ip_addr_string, port_uint16);

  // pthread_t - тип, хранящий id posix потока 
  pthread_t listener_tid, sender_tid;
  struct sender_thread_args args{.sock = sock,
                                 .port = port_uint16};

  // создаем потоки
  pthread_create(&sender_tid, NULL, sender_thread, &args);
  pthread_create(&listener_tid, NULL, listener_thread, &sock);

  pthread_join(sender_tid, NULL);
  pthread_join(listener_tid,
               NULL); // Ждём завершения (хотя это бесконечный цикл)

  close(sock);
  return 0;
}
