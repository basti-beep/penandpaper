import Tool from './tool'
import Map from '../components/Map.vue'
import * as Sim from '../simulation/simulation'
import eventBus from '../eventbus'
import * as B from '../simulation/building'

export default class ToolDoor extends Tool {
  isDrawing: boolean = false
  start: Sim.Point = new Sim.Point(0, 0)
  stop: Sim.Point = new Sim.Point(0, 0)

  accuracy: number = 4

  currentDoor: B.Door = new B.Door()

  onMouseDown (event: MouseEvent) : boolean {
    if (event.ctrlKey) {
      let worldPos = this.map.screenToWorldPos(new Sim.Point(event.offsetX, event.offsetY))
      if (event.button === 2) {
        this.map.removeDoorAt(worldPos.x, worldPos.y)
        this.map.requestRedraw()
      } else {
        this.start.x = Math.round(worldPos.x * this.accuracy) / this.accuracy
        this.start.y = Math.round(worldPos.y * this.accuracy) / this.accuracy
        this.stop.x = this.start.x
        this.stop.y = this.start.y
        this.isDrawing = true
      }
      return true
    } else {
      return super.onMouseDown(event)
    }
  }

  onMouseMove (event: MouseEvent) : boolean {
    let worldPos = this.map.screenToWorldPos(new Sim.Point(event.offsetX, event.offsetY))
    if (this.isDrawing) {
      this.stop.x = worldPos.x
      this.stop.y = worldPos.y
      // Draw the lines
      this.updateDoor()
      this.map.requestRedraw()
    } else {
      super.onMouseMove(event)
    }
    return false
  }

  onMouseUp (event: MouseEvent) : boolean {
    if (this.isDrawing) {
      // create a new door
      this.updateDoor()
      this.map.addDoor(this.currentDoor)
      this.currentDoor = new B.Door()
    }
    super.onMouseUp(event)
    this.isDrawing = false
    return false
  }

  updateDoor () {
    this.currentDoor.position.x = this.start.x
    this.currentDoor.position.y = this.start.y
    let delta = new Sim.Point(this.stop.x - this.start.x, this.stop.y - this.start.y)
    this.currentDoor.facing = delta.normalized()
    this.currentDoor.facing.toCardinalDirection()
    this.currentDoor.width = delta.length()
  }

  render (ctx: CanvasRenderingContext2D) {
    if (this.isDrawing) {
      ctx.lineWidth = this.map.computeLineWidth()
      this.currentDoor.render(ctx)

      let text = this.currentDoor.width.toFixed(1) + 'm'
      ctx.fillStyle = '#FFFFFF'
      this.map.setupScreenSpaceFont(ctx)
      let screenSpacePos = this.map.worldToScreenPos(new Sim.Point(this.currentDoor.position.x, this.currentDoor.position.y))
      let transform = ctx.getTransform()
      ctx.resetTransform()
      ctx.fillText(text, screenSpacePos.x + 10, screenSpacePos.y)
      ctx.setTransform(transform)
    }
  }
}
