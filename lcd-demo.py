#!/usr/bin/env python3
"""
Демо-клиент для LCD экрана G510s.
Подключается к g510s по TCP и рисует на экране.

Запуск:
    python3 lcd-demo.py

Экран появится в списке сразу (переключить на него: кнопка L1).
"""

import socket
import time
import math

HOST = '127.0.0.1'
PORT = 15550
W, H = 160, 43   # размер LCD в пикселях


def pixel(buf, x, y, val=1):
    if 0 <= x < W and 0 <= y < H:
        buf[y * W + x] = val


def hline(buf, y, x0, x1, val=1):
    for x in range(x0, x1 + 1):
        pixel(buf, x, y, val)


def vline(buf, x, y0, y1, val=1):
    for y in range(y0, y1 + 1):
        pixel(buf, x, y, val)


def rect(buf, x0, y0, x1, y1):
    hline(buf, y0, x0, x1)
    hline(buf, y1, x0, x1)
    vline(buf, x0, y0, y1)
    vline(buf, x1, y0, y1)


def make_frame(t):
    buf = bytearray(W * H)

    # рамка
    rect(buf, 0, 0, W - 1, H - 1)

    # анимированная синусоида
    for x in range(W):
        y = int((H // 2 - 2) + (H // 2 - 4) * math.sin((x / W * 4 * math.pi) + t))
        pixel(buf, x, y)

    # бегущая точка по верхней части
    dot_x = int((math.sin(t * 0.7) + 1) / 2 * (W - 4)) + 2
    dot_y = int((math.sin(t * 1.3) + 1) / 2 * (H // 2 - 4)) + 2
    for dx in range(-2, 3):
        for dy in range(-2, 3):
            if dx * dx + dy * dy <= 4:
                pixel(buf, dot_x + dx, dot_y + dy)

    return bytes(buf)


def main():
    print(f"Подключение к g510s на {HOST}:{PORT}...")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))

        hello = s.recv(64)
        print(f"Сервер: {hello.decode(errors='replace').strip()}")

        # объявляем формат 'G': 6880 байт на кадр (1 байт = 1 пиксель)
        s.sendall(b'G\x00\x00\x00')

        print("Отправка кадров. Нажмите Ctrl+C для выхода.")
        t = 0.0
        while True:
            frame = make_frame(t)
            s.sendall(frame)
            time.sleep(0.1)
            t += 0.15


if __name__ == '__main__':
    main()
