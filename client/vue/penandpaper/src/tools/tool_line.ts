import Tool from './tool'
import Map from '../components/Map.vue'
import * as Sim from '../simulation'
import eventBus from '../eventbus'

export default class ToolToken extends Tool {
  isDrawing: boolean = false
  lastLineStop: Sim.Point = new Sim.Point(0, 0)
  currentLineStop: Sim.Point = new Sim.Point(0, 0)

  onMouseDown (event: MouseEvent) : boolean {
    if (event.ctrlKey) {
      let worldPos = this.map.screenToWorldPos(new Sim.Point(event.offsetX, event.offsetY))
      this.lastLineStop.x = worldPos.x
      this.lastLineStop.y = worldPos.y
      this.currentLineStop.x = worldPos.x
      this.currentLineStop.y = worldPos.y
      this.isDrawing = true
      return true
    } else {
      return super.onMouseDown(event)
    }
  }

  onMouseMove (event: MouseEvent) : boolean {
    if (this.isDrawing) {
      let worldPos = this.map.screenToWorldPos(new Sim.Point(event.offsetX, event.offsetY))
      let lastLineScreen = this.map.worldToScreenPos(this.lastLineStop)
      this.currentLineStop.x = worldPos.x
      this.currentLineStop.y = worldPos.y
      if (Math.hypot(lastLineScreen.x - event.offsetX, lastLineScreen.y - event.offsetY) > this.map.$el.clientHeight / 20) {
        // create a new line
        let line = new Sim.Line()
        line.start.x = this.lastLineStop.x
        line.start.y = this.lastLineStop.y
        line.stop.x = worldPos.x
        line.stop.y = worldPos.y
        eventBus.$emit('/client/line/create', line)
        this.lastLineStop = worldPos
      }
      this.map.requestRedraw()
    } else {
      super.onMouseMove(event)
    }
    return false
  }

  onMouseUp (event: MouseEvent) : boolean {
    if (this.isDrawing) {
      // create a new line
      let worldPos = this.map.screenToWorldPos(new Sim.Point(event.offsetX, event.offsetY))
      let line = new Sim.Line()
      line.start.x = this.lastLineStop.x
      line.start.y = this.lastLineStop.y
      line.stop.x = worldPos.x
      line.stop.y = worldPos.y
      eventBus.$emit('/client/line/create', line)
      this.lastLineStop = worldPos
    }
    super.onMouseUp(event)
    this.isDrawing = false
    return false
  }

  render (ctx: CanvasRenderingContext2D) {
    if (this.isDrawing) {
      // Draw the lines
      ctx.strokeStyle = '#FFFFEE'
      ctx.lineWidth = this.map.computeLineWidth()
      ctx.beginPath()
      ctx.moveTo(this.lastLineStop.x, this.lastLineStop.y)
      ctx.lineTo(this.currentLineStop.x, this.currentLineStop.y)
      ctx.stroke()
    }
  }
}
