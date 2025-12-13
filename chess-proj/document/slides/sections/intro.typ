#import "../lib.typ": *

= Chess 介绍

== Chess 基本布局
#slide(align: center + horizon, composer: (1fr, 2fr))[
  #figure(caption: [基本布局])[#blue-board(starting-position)]
]

== Chess 基本走法
#slide(align: center + horizon, composer: (1fr, 1fr))[
  #figure(caption: [#chess-sym.pawn.black Pawn])[
    #blue-board(
      fen("8/8/8/4P3/8/8/8/8"),
      marked-squares: (
        "e6": marks.cross(paint: rgb("#5bc0ec")),
      ),
      arrows: (
        "e5 d6"
      ),
    )]
][
  #figure(caption: [#chess-sym.king.black King])[
    #blue-board(
      fen("8/8/8/4K3/8/8/8/8"),
      marked-squares: (
        "e6 d5 d4 d6 f6 f5 f4 e4": marks.cross(paint: rgb("#5bc0ec")),
      ),
    )]
]

#slide(align: center + horizon, composer: (1fr, 1fr))[
  #figure(caption: [#chess-sym.rook.black Rook])[
    #blue-board(
      fen("8/8/8/4R3/8/8/8/8"),
      marked-squares: (
        "e6 e4 e1 e2 e3 e7 e8 a5 b5 c5 d5 f5 g5 h5": marks.cross(paint: rgb("#5bc0ec")),
      ),
    )]
][
  #figure(caption: [#chess-sym.bishop.black Bishop])[
    #blue-board(
      fen("8/8/8/4B3/8/8/8/8"),
      marked-squares: (
        "d6 c7 b8 f6 g7 h8 d4 c3 b2 a1 f4 g3 h2": marks.cross(paint: rgb("#5bc0ec")),
      ),
    )]
]

#slide(align: center + horizon, composer: (1fr, 1fr))[
  #figure(caption: [#chess-sym.queen.black Queen])[
    #blue-board(
      fen("8/8/8/4Q3/8/8/8/8"),
      marked-squares: (
        "e6 e7 e8 e4 e2 e3 e1 a5 b5 c5 d5 f5 g5 h5 d6 c7 b8 f6 g7 h8 d4 c3 b2 a1 f4 g3 h2": marks.cross(
          paint: rgb("#5bc0ec"),
        ),
      ),
    )]
][
  #figure(caption: [#chess-sym.knight.black Knight])[
    #blue-board(
      fen("8/8/8/8/4N3/8/8/8"),
      marked-squares: (
        "d6 f6 c5 g5 c3 g3 d2 f2": marks.cross(paint: rgb("#5bc0ec")),
      ),
    )]
]

#slide[
  - *Castling* 王车易位

  - *Promotion* 升变
  - *En passant* 过路兵
]

== Material Points

#figure(
  table(
    columns: 7,
    align: (horizon,) * 7,
    rows: (auto, 2em),
    stroke: none,
    table.hline(y: 0, stroke: 2pt),
    table.hline(y: 1),
    table.hline(y: 2, stroke: 2pt),

    [*Piece*],
    [#chess-sym.pawn.black \ Pawn],
    [#chess-sym.knight.black \ Knight],
    [#chess-sym.bishop.black  \ Bishop],
    [#chess-sym.rook.black \ Rook],
    [#chess-sym.queen.black \ Queen],
    [#chess-sym.king.black \ King],
    [*Material Points*], [1], [3], [3], [5], [9], [∞],
  ),
  caption: [Chess Standard Material Points System],
)<tbl:material-points>

Piece values exist because calculating to checkmate in most positions is beyond even top computers. Thus, players aim primarily to create a material advantage, which is helpful to quantitatively *approximate the strength of an army of pieces*.#footnote[Chess piece relative value: https://en.wikipedia.org/wiki/Chess_piece_relative_value]
