/** Small re-export shim so GuidedScreen imports stay tidy. */
export { clearTake, serializeSession, takeKey } from '../../engine/session';
import type { SessionState } from '../../engine/session';

export function windowLayers(session: SessionState) {
  return [...session.config.plan.velocityMap].sort((a, b) => a.layer - b.layer);
}
