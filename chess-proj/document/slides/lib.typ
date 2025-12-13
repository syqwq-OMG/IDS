#import "src/slide.typ": *
#import "@preview/numbly:0.1.0": *
#import "@preview/board-n-pieces:0.7.0": *

#let project-style(body) = {

  show: show-theorion
  set text(lang: "en")
  show math.equation.where(block: false): math.display
  set heading(numbering: numbly("{1}.", default: "1.1"))

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
