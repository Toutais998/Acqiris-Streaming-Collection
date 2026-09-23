function test_scan_reconstruction
% 中文：小型合成文件验证保护尾段剔除、跨帧步进、非整帧尾行和时序拒绝；不是硬件模拟验证。
root = fileparts(mfilename('fullpath'));
folder = tempname; mkdir(folder);
input = fullfile(folder,'fixture.dat');
cfg = struct('status','complete','dataSaved',true,'sampleRate',1e6,'linePeriod',0.002, ...
    'frameFlyback',0.001,'recordSize',1024,'activeLineSamples',1000,'activeSampleOffset',0, ...
    'pixelsPerLine',4,'linesPerFrame',4,'timingToleranceSeconds',2e-6,'timingAnomalies',0);
fid=fopen(input,'wb'); assert(fid>=0);
for i=1:10
    fwrite(fid,[repmat(int16(i),1000,1);repmat(int16(32000),24,1)],'int16');
end
fclose(fid); writeConfig(input,cfg);
line=(1:10).'; record_index=line-1;
interval_s=[0;repmat(.002,9,1)]; interval_s([5 9])=.003;
timestamp_s=cumsum(interval_s);
writetable(table(line,record_index,timestamp_s,interval_s),[input '.markers.csv']);
oldFile=getenv('ACQ_DATA_FILE'); oldSelection=getenv('ACQ_FRAME_INDICES'); oldPreview=getenv('ACQ_ALLOW_TIMING_ANOMALIES');
cleanup=onCleanup(@() restoreEnv(oldFile,oldSelection,oldPreview));
setenv('ACQ_DATA_FILE',input); setenv('ACQ_FRAME_INDICES','all'); setenv('ACQ_ALLOW_TIMING_ANOMALIES','');
invoke(root);
r=load(fullfile(folder,'fixture_frame0002.mat'));
assert(isequal(r.image,repmat((5:8).',1,4)),'保护尾段混入像素或帧步进错误');
assert(r.stats.frameBoundaryIntervals==1 && r.stats.markerUnusualIntervals==0);
s=jsondecode(fileread(fullfile(folder,'fixture_multiframe_summary.json')));
assert(s.partialFrameLines==2 && s.availableFrames==2);
cfg.timingAnomalies=1; writeConfig(input,cfg);
rejected=false;
try, invoke(root); catch e, rejected=contains(e.message,'时序异常'); end
assert(rejected,'时序异常应拒绝正式重建');
setenv('ACQ_ALLOW_TIMING_ANOMALIES','1'); invoke(root);
assert(isfile(fullfile(folder,'fixture_frame0002_DIAGNOSTIC.mat')));
fprintf('RECONSTRUCTION_TEST_PASS output=%s\n',folder);
end
function invoke(root)
run(fullfile(root,'reconstruct_acqiris_scan.m'));
end
function writeConfig(input,cfg)
fid=fopen([input '.config.json'],'w'); assert(fid>=0); fprintf(fid,'%s',jsonencode(cfg)); fclose(fid);
end
function restoreEnv(a,b,c)
setenv('ACQ_DATA_FILE',a); setenv('ACQ_FRAME_INDICES',b); setenv('ACQ_ALLOW_TIMING_ANOMALIES',c);
end
