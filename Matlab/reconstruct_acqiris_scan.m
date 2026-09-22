%% Acqiris XY galvo line-stream 重建
% 中文说明：当前 C++ 程序将 StreamCh1 的 int32 元素直接写入 .dat 文件。
% 每个 int32 元素携带两个 int16 采样点，因此本脚本按 int16 顺序读取。
% 文件没有额外 header；每条 line 固定为 recordSize 个 int16 样本。

clear; clc;

%% 默认采集参数（与 CPP_IVIC_Streaming.cpp 保持一致）
dataFolder = 'D:\Acq_Storage';
filePattern = 'BNU_Mark25_Streaming_*.dat';
sampleRate = 1e9;
lineRate = 109.8;
linePeriod = 9104e-6; % 109.8 Hz 的实际扫描周期，作为 record 长度基准
activeDuty = 0.9;
pixelsPerLine = 512;
linesPerFrame = 512;
frameRate = 0.21;
activeLineSamples = round(linePeriod * activeDuty * sampleRate);
samplesPerPixel = activeLineSamples / pixelsPerLine;

assert(mod(activeLineSamples, pixelsPerLine) == 0, ...
    '有效 line 样本数不能被 512 整除，请检查 lineRate/采样率。');
samplesPerPixel = round(samplesPerPixel);

files = dir(fullfile(dataFolder, filePattern));
if isempty(files)
    error('未找到数据文件：%s', fullfile(dataFolder, filePattern));
end
[~, newest] = max([files.datenum]);
dataFile = fullfile(files(newest).folder, files(newest).name);
fprintf('读取文件：%s\n', dataFile);

%% 逐 line 读取并重建
% 中文说明：每个像素采用该像素时间窗内的均值，适合正值 PMT/SPAD 模拟信号。
fid = fopen(dataFile, 'rb');
if fid < 0
    error('无法打开文件：%s', dataFile);
end
cleanup = onCleanup(@() fclose(fid));

bytesPerSample = 2; % int16
fileBytes = files(newest).bytes;
availableSamples = floor(fileBytes / bytesPerSample);
maxLines = floor(availableSamples / activeLineSamples);
maxFrames = floor(maxLines / linesPerFrame);
if maxFrames < 1
    error('文件不足一帧：需要 %d samples，实际 %d samples。', ...
        activeLineSamples * linesPerFrame, availableSamples);
end

% 默认重建第一帧；可将 frameIndex 改为 2、3...读取后续帧。
frameIndex = 1;
firstLine = (frameIndex - 1) * linesPerFrame;
fseek(fid, firstLine * activeLineSamples * bytesPerSample, 'bof');
image = zeros(linesPerFrame, pixelsPerLine, 'double');

for line = 1:linesPerFrame
    raw = fread(fid, activeLineSamples, 'int16=>double');
    if numel(raw) ~= activeLineSamples
        error('第 %d 条 line 数据不完整，可能发生 overflow 或文件正在写入。', line);
    end
    pixelSamples = reshape(raw, samplesPerPixel, pixelsPerLine);
    image(line, :) = mean(pixelSamples, 1);
end

% 中文说明：按实际扫描方向显示；如振镜 Y 方向相反，可使用 flipud(image)。
figure('Color', 'w', 'Name', 'Acqiris reconstructed frame');
imagesc(image);
axis image; colormap gray; colorbar;
xlabel('X pixel (512)'); ylabel('Y line (512)');
title(sprintf('Acqiris frame %d | line %.3f Hz | %.3f Hz frame', ...
    frameIndex, lineRate, frameRate));

% 保存 MATLAB 结果，便于后续定量分析。
[~, stem] = fileparts(dataFile);
save(fullfile(dataFolder, [stem sprintf('_frame%04d.mat', frameIndex)]), ...
    'image', 'sampleRate', 'lineRate', 'activeDuty', 'pixelsPerLine', ...
    'linesPerFrame', 'frameRate', 'activeLineSamples', 'samplesPerPixel');
