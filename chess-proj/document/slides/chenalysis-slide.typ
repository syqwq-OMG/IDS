#import "lib.typ": *


#show: ecnu-theme.with(
  config-info(
    author: [syqwq],
    date: datetime.today().display(),
    institution: [ECNU Department of Data Science and Big Data Technology],
    title: [CHEnalySiS],
    subtitle: [国际象棋数据分析],
  ),
  config-common(
    slide-level: 2,
    new-section-slide-fn: none,
  ),
)
#show: project-style



#title-slide()

#outline-slide()

= Chess 介绍

== Chess 基本布局
#slide(align: center + horizon)[
  #figure(caption: [基本布局])[#blue-board(starting-position)]
]


= 参考资源
#slide[
  + #link("https://database.lichess.org/")[lichess.org Open Database]：国际象棋开源对局数据库
  + #link("https://github.com/Disservin/chess-library")[Disservin/chess-library]：`C++` 棋谱解析库
  + #link("https://github.com/nlohmann/json")[nlohmann/json]：`C++` JSON 数据解析库
  + #link("https://en.wikipedia.org/wiki/Chess_piece_relative_value")[Chess piece relative value Wikipedia]
]

