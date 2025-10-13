#import "../lib.typ": *
#import "@preview/callisto:0.2.4"
#show: doc.with(hw-id: 4)

#let (render, result) = callisto.config(nb: json("hw4.ipynb"))

// 注意编码，还有空行
#render(0)
