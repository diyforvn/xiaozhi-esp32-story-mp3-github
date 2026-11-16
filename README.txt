Robot base cho xiaozhi-esp32-story-mp3-github

Cai dat (tu goc repo, ket qua giong nhau du file dang CRLF hay LF):
  1) Giai nen xiaozhi_robot_base.zip vao goc repo (se tao main/robot_base, docs/ROBOT_BASE.md, tests/,
     va 2 file apply_robot_patch.py + Kconfig.robot.snippet).
  2) python apply_robot_patch.py
     -> tu sua 6 file co san: CMakeLists.txt, Kconfig.projbuild, application.cc,
        compact_wifi_board.cc, config.h (OLED), compact_wifi_board_lcd.cc.
     Chay lai nhieu lan khong sao. File nao khac ban goc thi script bao ro de sua tay.
  3) idf.py menuconfig -> Xiaozhi Assistant -> Robot Base -> Enable
  4) Doc docs/ROBOT_BASE.md truoc khi cap nguon cho motor.
