#[path="/home/scion/scion-star2lte/xnu-deps/build/panvk-native/fb-fetch-repo/crates/metal2vulkan-runtime/src/host_snapshot.rs"]
mod host_snapshot;
use host_snapshot::HostSnapshot;
use std::time::Instant;

fn bench(size: usize, rounds: usize) {
    let mut caller=vec![0x31u8;size];let mut gpu=vec![0u8;size];
    let mut snapshot=HostSnapshot::new(size).unwrap();
    let start=Instant::now();let first=snapshot.upload(&caller,&mut gpu, |_|{});
    let initial=start.elapsed().as_micros();
    gpu[size/2]=0x72;
    let start=Instant::now();let mut unchanged=0;
    for _ in 0..rounds {unchanged+=snapshot.upload(std::hint::black_box(&caller),&mut gpu, |_|{});}
    let warm=start.elapsed().as_micros();assert_eq!(unchanged,0);assert_eq!(gpu[size/2],0x72);
    caller[3]=0x44;caller[size-1]=0x55;
    let start=Instant::now();let sparse=snapshot.upload(&caller,&mut gpu, |_|{});
    let sparse_us=start.elapsed().as_micros();assert_eq!(sparse,2);assert_eq!(gpu[size/2],0x72);
    caller.iter_mut().for_each(|v|*v^=0x80);
    let start=Instant::now();let changed=snapshot.upload(&caller,&mut gpu, |_|{});
    let changed_us=start.elapsed().as_micros();assert_eq!(changed,size as u64);assert_eq!(caller,gpu);
    let start=Instant::now();let mut chunk=[0u8;4096];
    for (i,input) in gpu.chunks(4096).enumerate() {
        unsafe{host_snapshot::read_chunk(input.as_ptr(),&mut chunk[..input.len()])};
        snapshot.downloaded(i*4096,&chunk[..input.len()]);
    }
    let download=start.elapsed().as_micros();
    println!("HOST_SNAPSHOT_BENCH bytes={size} rounds={rounds} first_bytes={first} initial_us={initial} unchanged_total_us={warm} sparse_bytes={sparse} sparse_us={sparse_us} full_change_us={changed_us} staging_us={download}");
}
fn main(){bench(12*1024*1024,16);bench(64*1024*1024,8);
    if let Ok(s)=std::fs::read_to_string("/proc/self/status") {for l in s.lines().filter(|l|l.starts_with("VmHWM:")){println!("{l}");}}
}
