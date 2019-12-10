export class Rectangle {
  minx : number = 0;
  maxx : number = 0;
  miny : number = 0;
  maxy : number = 0;
}

export class Point {
  x: number = 0;
  y: number = 0;

  constructor (x: number, y: number) {
    this.x = x
    this.y = y
  }
}

export class Color {
  r: number = 0
  g: number = 0
  b: number = 0

  constructor (r: number, g: number, b: number) {
    this.r = r
    this.g = g
    this.b = b
  }

  toHex () {
    return '#' + this.r.toString(16).padStart(2, '0') + this.g.toString(16).padStart(2, '0') + this.b.toString(16).padStart(2, '0')
  }
}

export class Token {
  x: number = 0;
  y: number = 0;
  radius: number = 0.25;
  id: number = -1;
  isFoe: boolean = false
  color: Color = new Color(255, 0, 255)
}

export const TOKEN_COLORS = [
  new Color(240, 50, 50), //  red
  new Color(176, 30, 90), //  burgund
  new Color(201, 20, 201), // pink
  new Color(120, 61, 196), // purple
  new Color(24, 100, 171), // blue
  new Color(24, 172, 171), // turquoise
  new Color(8, 127, 91), //   blue-green
  new Color(92, 148, 13), //  red-green
  new Color(217, 72, 15), //  orange
  new Color(129, 96, 65), //   brown
  new Color(201, 201, 30) //   yellow
]

// Event specific data types
export class TokenMoveOrder {
  x: number = 0
  y: number = 0
  token: Token = new Token()
}
