<p align="center"><img width="50%" src="docs/images/LabPPBot.png"/></p>
<hr/>
# LabPPBot_Cpp ![C++](https://img.shields.io/badge/-C++-505050?logo=c%2B%2B&style=flat)

**LabPPBot_Cpp는 카카오톡 메신저에서 사용가능한 자동화 봇 클라이언트입니다.**

**Git을 이용한 형상관리 및 명령어를 통한 자동 업데이트를 지원하며, 사용자가 직접 명령어 및 봇의 행동을 제어할 수 있습니다.**

# Build Pipelines
|         |  `Clang-Format`  |`Build`     | 
| :---:   |      :---:     | :---:      |
| Status | [![Status](https://github.com/changdae20/LabPPBot_Cpp/actions/workflows/formatting.yml/badge.svg)](https://github.com/changdae20/LabPPBot_Cpp/actions) | [![Status](https://github.com/changdae20/LabPPBot_Cpp/actions/workflows/main.yml/badge.svg)](https://github.com/changdae20/LabPPBot_Cpp/actions) |

# Example
아래 예시는 Git을 이용한 자동 업데이트 기능입니다.
<p align="center">
<img width="60%" src="docs/images/update_example(1).png"/> <img width="30%" src="docs/images/update_example(2).gif"/>
</p>

# Dependencies
* Formatting Library : [fmt](https://github.com/fmtlib/fmt)
* Data Format : [Protobuf](https://github.com/protocolbuffers/protobuf)
* Compiler : [MSVC](https://visualstudio.microsoft.com)
* Package Manager : [vcpkg](https://github.com/microsoft/vcpkg)

# TODOs
* HTTPS client 추가
* 주석 기반 자동 도움말 생성 기능 추가 (like swagger, dartdoc, sphinx, ...)

# License

**The MIT License (MIT)**

**Copyright (c) 2022 changdae20.**
