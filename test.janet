#!/usr/bin/env janet

(import /build/libjanetui :as ui)

(def menu (ui/menu "File"))

(ui/menu/append-item menu "Save")
(ui/menu/append-item menu "Save As")

(let [w (ui/window "Hello" 400 300 true)
      box (ui/vertical-box)
      b (ui/button "Bite me!")
      cb (ui/checkbox "Check me!")
      tabs (ui/tab)
      pbar (ui/progress-bar)
      cbox (ui/editable-combobox)
      en (ui/entry)]

  (ui/window/set-child w tabs)
  (ui/tab/append tabs "first tab" box)
  (ui/tab/margined tabs 0 true)
  (ui/window/margined w true)
  (ui/progress-bar/value pbar 45)

  (ui/editable-combobox/append cbox "hello")
  (ui/editable-combobox/append cbox "lovely")
  (ui/editable-combobox/append cbox "world")

  (ui/box/append box b)
  (ui/box/append box cb)
  (ui/box/append box en)
  (ui/box/append box pbar)
  (ui/box/append box (ui/label "Progress: 22/10102"))
  (ui/box/append box (ui/slider 0 100))
  (ui/box/append box (ui/horizontal-separator))
  (ui/box/append box cbox)

  (ui/button/on-clicked b (fn [] (ui/open-file w)))
  (ui/entry/on-changed en (fn [] (print "Entry value: " (ui/entry/text en))))

  (ui/show w))

(ui/main)
