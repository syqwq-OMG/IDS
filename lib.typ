#import "@preview/zebraw:0.5.5": *
#import "@preview/showybox:2.0.4": showybox
#import "@preview/theorion:0.4.0": remark as oremark

#let link-color = rgb("#227591")

#let doc(name: "孙育泉", id: 10234900421, hw-id: 1, body) = {
  let (en-font, cn-font) = ("New Computer Modern", "Source Han Serif SC")

  set text(font: (en-font, cn-font), weight: 200)
  // set text(font: (en-font, cn-font))
  show raw: set text(font: (
    (name: "Consolas Nerd Font", covers: "latin-in-cjk"),
    "Microsoft YaHei",
  ))

  set heading(numbering: "I.1")
  set par(first-line-indent: 2em)
  set page(numbering: "1", header: context {
    if here().page() == 1 { return none }
    align(center, box(image("src/ecnu-logo.svg"), height: 45%))
  })

  set outline.entry(fill: repeat(math.dot, gap: 0.1em))
  show outline.entry: it => text(fill: link-color, it)

  show link: it => text(fill: link-color, it)
  show: zebraw.with(background-color: luma(249), indentation: 4, hanging-indent: true)
  show "。": ". "
  show math.equation: it => {
    show regex("\p{script=Han}"): set text(font: cn-font)
    it
  }
  show heading.where(level: 1): it => text(size: 16pt, it)
  show heading.where(level: 2): it => text(size: 14pt, it)

  set enum(numbering: "(1)")


  align(center, underline(stroke: red + 0.7pt, offset: 5pt, (
    underline(stroke: red + 0.7pt, offset: 3pt, text(size: 20pt, weight: 500, [
      数据科学导论 -HW #hw-id 报告
    ]))
  )))

  align(center, text(size: 13pt)[
    #name #id \
    #datetime.today().display("[year].[month].[day]")
  ])

  outline(title: " 总览", depth: 2)

  body
}

#let remark = oremark.with(fill: rgb("#5b67d8"))
