# ST7796U2 framebuffer test

Run after the ST7796U2 panel driver registers `/dev/fb0`:

```text
nsh> st7796_test
nsh> st7796_test --cycles 10
```

The command requires a 480×320 RGB565 framebuffer, displays solid colors,
quadrants, corner markers, and a partial rectangle, and issues `FBIO_UPDATE`
after each pattern. Successful ioctl calls prove the software path executed;
visible colors and orientation still require physical inspection.
