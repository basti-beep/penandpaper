
export default class Matrix {
  data: Float32Array = new Float32Array(16)

  constructor() {
    this.setIdentity()
  }

  setIdentity () {
    for (let x = 0; x < 4; x++) {
      for (let y = 0; y < 4; y++) {
        this.set(x, y, x === y ? 1 : 0)
      } 
    }
  }

  at (row: number, col: number) : number {
    return this.data[row + col * 4]
  }

  set (row: number, col: number, val: number) {
    this.data[row + col * 4] = val
  }

  /**
   * @return The result of (this) * (other)
   */
  mul (other: Matrix) : Matrix {
    let m = new Matrix()
    for (let row = 0; row < 4; row++) {
      for (let col = 0; col < 4; col++) {
        let val = 0
        for (let i = 0; i < 4; ++i) {
          val += this.at(row, i) * other.at(i, col)
        }
        m.set(row, col, val)
      } 
    }
    return m
  }

  translate (x: number = 0, y: number = 0, z: number = 0) {
    let m = new Matrix()
    m.set(0, 3, x)
    m.set(1, 3, y)
    m.set(2, 3, z)
    this.data = m.mul(this).data
  }

  scale (x: number = 1, y: number = 1, z: number = 1) {
    let m = new Matrix()
    m.set(0, 0, x)
    m.set(1, 1, y)
    m.set(2, 2, z)
    this.data = m.mul(this).data
  }
}