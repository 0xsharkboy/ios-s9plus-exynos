//! Cached CPU history for an emulated no-copy buffer. This is the last host value
//! transferred, not a claim that the GPU's bytes have stayed unchanged meanwhile.

use std::collections::TryReserveError;
use std::ops::Range;

/// One bounded, buffer-sized snapshot, released with that buffer.
pub(super) struct HostSnapshot {
    bytes: Vec<u8>,
    initialized: bool,
}

impl HostSnapshot {
    pub(super) fn new(length: usize) -> Result<Self, TryReserveError> {
        let mut bytes = Vec::new();
        bytes.try_reserve_exact(length)?;
        bytes.resize(length, 0);
        Ok(Self { bytes, initialized: false })
    }

    /// Transfer CPU changes without reading the GPU mapping or replacing bytes
    /// that only the GPU changed. `written` bounds each chunk's actual CPU stores
    /// for cache maintenance; gaps in that bound are not themselves copied.
    pub(super) fn upload(
        &mut self,
        caller: &[u8],
        gpu: &mut [u8],
        mut written: impl FnMut(Range<usize>),
    ) -> u64 {
        assert_eq!(caller.len(), self.bytes.len());
        assert_eq!(gpu.len(), caller.len());
        if !self.initialized {
            gpu.copy_from_slice(caller);
            self.bytes.copy_from_slice(caller);
            self.initialized = true;
            if !caller.is_empty() { written(0..caller.len()); }
            return caller.len() as u64;
        }
        let mut transferred = 0;
        for (chunk, (from, previous)) in caller.chunks(4096).zip(self.bytes.chunks_mut(4096)).enumerate() {
            if from == previous { continue; }
            let base = chunk * 4096;
            let mut at = 0;
            let mut first = from.len();
            let mut last = 0;
            while at < from.len() {
                if from[at] == previous[at] { at += 1; continue; }
                let begin = at;
                while at < from.len() && from[at] != previous[at] { at += 1; }
                gpu[base + begin..base + at].copy_from_slice(&from[begin..at]);
                transferred += (at - begin) as u64;
                first = first.min(begin);
                last = at;
            }
            previous.copy_from_slice(from);
            written(base + first..base + last);
        }
        transferred
    }

    /// The GPU's completed output was made visible to the caller. Future uploads
    /// compare CPU changes with these bytes instead of rereading GPU memory.
    pub(super) fn downloaded(&mut self, offset: usize, bytes: &[u8]) {
        self.bytes[offset..offset + bytes.len()].copy_from_slice(bytes);
    }
}

#[cfg(test)]
mod tests {
    use super::HostSnapshot;

    #[test]
    fn unchanged_cpu_input_does_not_replace_a_new_gpu_value() {
        let mut snapshot = HostSnapshot::new(9001).unwrap();
        let caller = vec![17; 9001];let mut gpu = vec![99; 9001];
        assert_eq!(snapshot.upload(&caller, &mut gpu, |_| {}), 9001);
        gpu[50] = 88;gpu[9000] = 77;
        assert_eq!(snapshot.upload(&caller, &mut gpu, |_| panic!("unchanged CPU input")), 0);
        assert_eq!((gpu[50], gpu[9000]), (88, 77));
    }

    #[test]
    fn disjoint_cpu_and_gpu_changes_in_the_same_chunk_both_survive() {
        let mut snapshot = HostSnapshot::new(8193).unwrap();
        let mut caller = vec![0; 8193];let mut gpu = caller.clone();
        snapshot.upload(&caller, &mut gpu, |_| {});
        gpu[1] = 90;gpu[3] = 91;gpu[4097] = 92;
        caller[0] = 31;caller[2] = 32;caller[4] = 33;caller[8192] = 34;
        let mut spans = Vec::new();
        assert_eq!(snapshot.upload(&caller, &mut gpu, |span| spans.push(span)), 4);
        assert_eq!(spans, [0..5, 8192..8193]);
        assert_eq!(&gpu[..5], &[31, 90, 32, 91, 33]);
        assert_eq!((gpu[4097], gpu[8192]), (92, 34));
    }

    #[test]
    fn completed_output_becomes_the_next_cpu_comparison_baseline() {
        let mut snapshot = HostSnapshot::new(128).unwrap();
        let mut caller = vec![1; 128];let mut gpu = caller.clone();
        snapshot.upload(&caller, &mut gpu, |_| {});
        gpu[17] = 42;caller.copy_from_slice(&gpu);snapshot.downloaded(0, &gpu);
        assert_eq!(snapshot.upload(&caller, &mut gpu, |_| panic!("output is already visible")), 0);
        caller[18] = 43;
        assert_eq!(snapshot.upload(&caller, &mut gpu, |_| {}), 1);
        assert_eq!(&gpu[17..19], &[42, 43]);
    }

    #[test]
    fn changed_runs_cover_chunks_and_partial_last_chunks_exactly() {
        let mut snapshot = HostSnapshot::new(8201).unwrap();
        let mut caller = vec![0; 8201];let mut gpu = caller.clone();
        snapshot.upload(&caller, &mut gpu, |_| {});
        caller[4090..8201].fill(19);
        let mut spans = Vec::new();
        assert_eq!(snapshot.upload(&caller, &mut gpu, |span| spans.push(span)), 4111);
        assert_eq!(spans, [4090..4096, 4096..8192, 8192..8201]);
        assert_eq!(gpu, caller);
    }

    #[test]
    fn impossible_allocation_is_reported_without_an_abort() {
        assert!(HostSnapshot::new(usize::MAX).is_err());
    }
}
