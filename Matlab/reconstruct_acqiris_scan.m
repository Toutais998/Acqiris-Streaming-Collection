%% Acqiris整帧重建：纯int16、小端、无头，每行8193600点
% 中文：ACQ_DATA_FILE可指定输入，否则选择最新文件。逐行读取，不整体加载8GB。
clear; clc;
dataFolder = 'D:\Acq_Storage';
sampleRate = 1e9;
linePeriod = 9104e-6;
lineRate = 1/linePeriod;
activeDuty = 0.9;
pixelsPerLine = 512;
linesPerFrame = 512;
frameRate = lineRate/linesPerFrame;
activeLineSamples = round(linePeriod*activeDuty*sampleRate);
samplesPerPixel = activeLineSamples/pixelsPerLine;
% 中文：16003.125点/像素不能reshape；边界分箱覆盖所有样本，不补零、不丢点。
pixelEdges = round(linspace(0,activeLineSamples,pixelsPerLine+1));
pixelCounts = diff(pixelEdges);
assert(sum(pixelCounts)==activeLineSamples && all(pixelCounts>0));
dataFile = getenv('ACQ_DATA_FILE');
if isempty(dataFile)
    files = dir(fullfile(dataFolder,'BNU_Mark25_Streaming_*.dat'));
    assert(~isempty(files),'未找到采集dat文件');
    [~,newest] = max([files.datenum]);
    dataFile = fullfile(files(newest).folder,files(newest).name);
end
info = dir(dataFile);
assert(isscalar(info),'数据文件不存在');
assert(mod(info.bytes,activeLineSamples*2)==0,'文件末尾不是完整record');
availableLines = info.bytes/(activeLineSamples*2);
assert(availableLines>=linesPerFrame,'文件不足512条完整line');
frameIndex = 1;
fid = fopen(dataFile,'rb','ieee-le');
assert(fid>=0,'不能打开数据文件');
cleanup = onCleanup(@() fclose(fid));
image = zeros(linesPerFrame,pixelsPerLine);
rawMinimum = inf; rawMaximum = -inf;
lowRailCount = 0; highRailCount = 0;
fprintf('INPUT %s\n',dataFile);
for line = 1:linesPerFrame
    raw = fread(fid,activeLineSamples,'int16=>double');
    assert(numel(raw)==activeLineSamples,'record数据不完整');
    sums = [0;cumsum(raw)];
    image(line,:) = (diff(sums(pixelEdges+1))./pixelCounts(:)).';
    rawMinimum = min(rawMinimum,min(raw)); rawMaximum = max(rawMaximum,max(raw));
    lowRailCount = lowRailCount+nnz(raw==-32768);
    highRailCount = highRailCount+nnz(raw==32767);
    if line==1
        waveform = raw(1:8000);
        probe = raw(1:min(1000000,numel(raw)));
        sorted = sort(probe);
        lowLevelCode = median(sorted(1:floor(end/4)));
        highLevelCode = median(sorted(ceil(3*end/4):end));
        threshold = (lowLevelCode+highLevelCode)/2;
        rising = find(diff(probe>threshold)==1);
        if numel(rising)>2
            measuredFrequency = sampleRate/median(diff(rising));
        else
            measuredFrequency = NaN;
        end
    end
end
clear cleanup;
markerFile = [dataFile '.markers.csv'];
markers = table();
if isfile(markerFile)
    markers = readtable(markerFile);
    assert(height(markers)>=512 && all(diff(markers.record_index(1:512))==1),'marker索引不连续');
end
stats = struct('inputFile',dataFile,'fileBytes',info.bytes,'availableLines',availableLines, ...
    'imageSize',size(image),'rawMinimum',rawMinimum,'rawMaximum',rawMaximum, ...
    'meanCode',mean(image(:)),'stdCode',std(image(:)),'minPixelCode',min(image(:)), ...
    'maxPixelCode',max(image(:)),'lowLevelCode',lowLevelCode,'highLevelCode',highLevelCode, ...
    'lowRailFraction',lowRailCount/(activeLineSamples*512), ...
    'highRailFraction',highRailCount/(activeLineSamples*512), ...
    'estimatedFrequencyHz',measuredFrequency,'cyclesPerPixelAt2_5MHz',samplesPerPixel/sampleRate*2.5e6);
if ~isempty(markers)
    stats.markerMinInterval = min(markers.interval_s(2:512));
    stats.markerMaxInterval = max(markers.interval_s(2:512));
    stats.markerUnusualIntervals = nnz(abs(markers.interval_s(2:512)-linePeriod)>linePeriod*0.01);
end
[outputFolder,stem] = fileparts(dataFile);
base = fullfile(outputFolder,[stem '_frame0001']);
% 中文：原图使用固定ADC满量程灰度；自动对比度增强图单独输出，不代表真实幅度变化很大。
imwrite(uint16(round(image+32768)),[base '.png']);
span = max(image(:))-min(image(:));
enhanced = (image-min(image(:)))/max(span,eps);
imwrite(uint16(round(enhanced*65535)),[base '_contrast.png']);
save([base '.mat'],'image','stats','markers','sampleRate','lineRate','linePeriod', ...
    'activeDuty','pixelsPerLine','linesPerFrame','frameRate','activeLineSamples', ...
    'samplesPerPixel','pixelEdges','pixelCounts','waveform');
fig = figure('Visible','off','Color','w','Position',[100 100 1500 480]);
tiledlayout(1,3);
nexttile; imagesc(image,[-32768 32767]); axis image; colormap gray; colorbar;
title('512 x 512 | fixed ADC scale'); xlabel('X pixel'); ylabel('Y line');
nexttile; imagesc(image); axis image; colorbar;
title('Auto contrast (small variations amplified)'); xlabel('X pixel'); ylabel('Y line');
nexttile; plot((0:numel(waveform)-1)/sampleRate*1e6,waveform);
xlim([0 3]); xlabel('Time (us)'); ylabel('ADC code'); grid on;
title(sprintf('Raw waveform | estimated %.4f MHz',measuredFrequency/1e6));
exportgraphics(fig,[base '_preview.png'],'Resolution',150); close(fig);
jsonFile = fopen([base '_summary.json'],'w','n','UTF-8');
assert(jsonFile>=0); fprintf(jsonFile,'%s\n',jsonencode(stats,PrettyPrint=true)); fclose(jsonFile);
fprintf('RECONSTRUCTION_RESULT %s\n',jsonencode(stats));
fprintf('OUTPUT_BASE %s\n',base);
