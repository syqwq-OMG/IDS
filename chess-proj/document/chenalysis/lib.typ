#import "@preview/gentle-clues:1.2.0": *
#import "@preview/itemize:0.2.0" as _itemize
#import "@preview/board-n-pieces:0.7.0": *

#let cn-serif = "Source Han Serif SC"
#let cn-sans = "Source Han Sans SC"
#let en-serif = "New Computer Modern"
#let en-sans = "New Computer Modern Sans"
#let serif = (en-serif, cn-serif)
#let sans = (en-sans, cn-sans)


#let doc(name: "syqwq", id: 0, title: "title", body) = {
  set text(font: (..serif, "Noto Sans Symbols 2"), lang: "zh")

  set heading(numbering: "1.")
  show heading.where(level: 1): it => {
    it
    v(5pt)
  }

  set page(numbering: "1")
  set par(first-line-indent: (all: true, amount: 2em), justify: true)
  // show figure: set block(breakable: true)
  set figure(placement: auto)

  show: _itemize.default-list
  show: _itemize.default-enum-list
  set enum(numbering: "(1)")

  show "。": ". "
  show link: set text(fill: rgb("#0e6388"))
  show ref: set text(fill: rgb("#0e6388"))

  align(center)[
    #text(size: 17pt, title)
    #v(1pt)
    #text(size: 14pt)[#name #id]
  ]


  body
}

#let blue-board = board.with(
  display-numbers: true,

  white-square-fill: rgb("#d2eeea"),
  black-square-fill: rgb("#567f96"),
  white-mark: marks.cross(paint: rgb("#2bcbC6")),
  black-mark: marks.cross(paint: rgb("#2bcbC6")),
  arrow-fill: rgb("#38f442df"),
  arrow-thickness: 0.25cm,

  stroke: 0.8pt + black,
)