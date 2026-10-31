//! Read-only timing and transfer counters for the board's local diagnostics.
//! These queries do not wait for GPU work, modify a policy or access texels.
use super::{get, guard, NmbCommandBuffer};

#[unsafe(no_mangle)]
pub extern "C" fn nmb_command_buffer_kernel_time(command_buffer: *const NmbCommandBuffer, end: bool) -> f64 {
    guard(0.0, || {
        let buffer = &get(command_buffer)?.0;
        Ok(if end { buffer.kernel_end_time() } else { buffer.kernel_start_time() })
    })
}

