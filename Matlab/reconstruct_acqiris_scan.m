%% Acqiris单帧/多帧重建：从配置侧文件读取窗口，逐行处理，不整体加载原始文件
% 中文：ACQ_DATA_FILE指定输入；ACQ_FRAME_INDICES为逗号分隔帧号，空或all表示全部。
clear; clc;
dataFolder = 'D:\Acq_Storage';
dataFile = getenv('ACQ_DATA_FILE');
if isempty(dataFile)
    files = dir(fullfile(dataFolder,'BNU_Mark25_Streaming_*.dat'));
    [~,order] = sort([files.datenum],'descend');
    for k=order
        candidate = fullfile(files(k).folder,files(k).name);
        configFile = [candidate '.config.json'];
        if isfile(configFile)
            c = jsondecode(fileread(configFile));
            if ~strcmp(c.status,'complete') || ~c.dataSaved, continue; end
        end
        if files(k).bytes==0, continue; end
        dataFile = candidate; break;
    end
end
assert(~isempty(dataFile),'没有完整采集文件');
% 中文：兼容旧9104us文件；新版优先读取真实记录配置，禁止按新窗口误读旧文件。
sampleRate = 1e9; linePeriod = 9104e-6; activeDuty = 0.9;
pixelsPerLine = 512; linesPerFrame = 512;
activeLineSamples = round(linePeriod*activeDuty*sampleRate);
config = struct;
if isfile([dataFile '.config.json'])
    config = jsondecode(fileread([dataFile '.config.json']));
    assert(strcmp(config.status,'complete') && config.dataSaved,'失败或不落盘诊断不能重建为完整采集');
    sampleRate = config.sampleRate; linePeriod = config.linePeriod;
    activeLineSamples = config.recordSize;
    pixelsPerLine = config.pixelsPerLine; linesPerFrame = config.linesPerFrame;
    activeDuty = activeLineSamples/sampleRate/linePeriod;
end
lineRate = 1/linePeriod; frameRate = lineRate/linesPerFrame;
samplesPerPixel = activeLineSamples/pixelsPerLine;
pixelEdges = round(linspace(0,activeLineSamples,pixelsPerLine+1));
pixelCounts = diff(pixelEdges);
info = dir(dataFile);
assert(isscalar(info) && mod(info.bytes,activeLineSamples*2)==0,'文件不是完整记录');
availableLines = info.bytes/(activeLineSamples*2);
availableFrames = floor(availableLines/linesPerFrame);
assert(availableFrames>=1,'文件不足一帧');
selection = getenv('ACQ_FRAME_INDICES');
if isempty(selection) || strcmpi(selection,'all')
    frameIndices = 1:availableFrames;
else
    frameIndices = str2double(strsplit(selection,','));
    assert(all(isfinite(frameIndices) & frameIndices>=1 & frameIndices<=availableFrames & frameIndices==floor(frameIndices)));
end
markers = table;
if isfile([dataFile '.markers.csv'])
    markers = readtable([dataFile '.markers.csv']);
    assert(height(markers)==availableLines,'marker与文件行数不符');
    assert(all(mod(diff(markers.record_index),2^24)==1),'marker索引不连续');
end
fid = fopen(dataFile,'rb','ieee-le');
assert(fid>=0); cleanup = onCleanup(@() fclose(fid));
images = zeros(linesPerFrame,pixelsPerLine,numel(frameIndices),'single');
frameStats = cell(1,numel(frameIndices));
waveform = [];
fprintf('INPUT %s | available_frames=%d | selected=%d | record=%d\n',dataFile,availableFrames,numel(frameIndices),activeLineSamples);
startTime = tic;
for fi = 1:numel(frameIndices)
    frameIndex = frameIndices(fi);
    assert(fseek(fid,(frameIndex-1)*linesPerFrame*activeLineSamples*2,'bof')==0);
    image = zeros(linesPerFrame,pixelsPerLine);
    rawMinimum = inf; rawMaximum = -inf; lowRailCount = 0; highRailCount = 0;
    for line = 1:linesPerFrame
        raw = fread(fid,activeLineSamples,'*int16');
        assert(numel(raw)==activeLineSamples,'不完整record');
        if samplesPerPixel==floor(samplesPerPixel)
            % 中文：新配置每像素12000点可直接分箱，省去大型double前缀和。
            image(line,:) = mean(reshape(raw,samplesPerPixel,pixelsPerLine),1);
        else
            sums = [0;cumsum(double(raw))];
            image(line,:) = (diff(sums(pixelEdges+1))./pixelCounts(:)).';
        end
        rawMinimum = min(rawMinimum,double(min(raw))); rawMaximum = max(rawMaximum,double(max(raw)));
        lowRailCount = lowRailCount+nnz(raw==-32768); highRailCount = highRailCount+nnz(raw==32767);
        if line==1
            probe = double(raw(1:min(1000000,numel(raw))));
            sorted = sort(probe);
            lowLevelCode = median(sorted(1:floor(end/4)));
            highLevelCode = median(sorted(ceil(3*end/4):end));
            % 中文：迟滞交越排除慢斜坡上的噪声重复穿越，避免100kHz误报200kHz。
            lower = lowLevelCode+0.35*(highLevelCode-lowLevelCode);
            upper = lowLevelCode+0.65*(highLevelCode-lowLevelCode);
            highEvents = find(diff(probe>upper)==1);
            lowEvents = find(diff(probe<lower)==1);
            events = [highEvents;lowEvents]; labels = [ones(size(highEvents));zeros(size(lowEvents))];
            [events,order] = sort(events); labels = labels(order);
            rising = events(labels==1 & [true;diff(labels)~=0]);
            periods = diff(rising);
            measuredFrequency = sampleRate/median(periods);
            if fi==1, waveform = probe(1:min(100000,numel(probe))); end
        end
    end
    stats = struct('inputFile',dataFile,'frameIndex',frameIndex,'imageSize',size(image), ...
        'meanCode',mean(image(:)),'stdCode',std(image(:)), ...
        'minPixelCode',min(image(:)),'maxPixelCode',max(image(:)), ...
        'rawMinimum',rawMinimum,'rawMaximum',rawMaximum, ...
        'lowRailFraction',lowRailCount/(activeLineSamples*linesPerFrame), ...
        'highRailFraction',highRailCount/(activeLineSamples*linesPerFrame), ...
        'estimatedFrequencyHz',measuredFrequency,'cyclesPerPixel',samplesPerPixel/sampleRate*measuredFrequency);
    if ~isempty(markers)
        rows = (frameIndex-1)*linesPerFrame+(1:linesPerFrame);
        delta = markers.interval_s(rows(2:end));
        stats.markerMinInterval = min(delta); stats.markerMaxInterval = max(delta);
        stats.markerUnusualIntervals = nnz(abs(delta-linePeriod)>linePeriod*0.01);
    end
    images(:,:,fi) = single(image); frameStats{fi} = stats;
    [folder,stem] = fileparts(dataFile);
    base = fullfile(folder,sprintf('%s_frame%04d',stem,frameIndex));
    imwrite(uint16(round(image+32768)),[base '.png']);
    save([base '.mat'],'image','stats','config','sampleRate','linePeriod','lineRate','frameRate', ...
        'activeLineSamples','samplesPerPixel','pixelEdges','pixelCounts','activeDuty','pixelsPerLine','linesPerFrame');
    jf = fopen([base '_summary.json'],'w','n','UTF-8');
    assert(jf>=0); fprintf(jf,'%s\n',jsonencode(stats,PrettyPrint=true)); fclose(jf);
    fprintf('FRAME_RESULT %s\n',jsonencode(stats));
end
clear cleanup;
% 中文：多帧增强统一使用同一色标，避免每帧自动拉伸掩盖帧间变化。
lo = double(min(images(:))); hi = double(max(images(:)));
for fi=1:numel(frameIndices)
    enhanced = (double(images(:,:,fi))-lo)/max(hi-lo,eps);
    imwrite(uint16(round(enhanced*65535)),fullfile(folder,sprintf('%s_frame%04d_contrast.png',stem,frameIndices(fi))));
end
baseAll = fullfile(folder,[stem '_multiframe']);
save([baseAll '.mat'],'images','frameStats','frameIndices','markers','config','waveform','sampleRate','linePeriod','samplesPerPixel');
fig = figure('Visible','off','Color','w','Position',[50 50 1500 520]);
tiledlayout(1,3);
nexttile; imagesc(images(:,:,1),[lo max(hi,lo+eps)]); axis image; colormap gray; colorbar;
title(sprintf('Frame %d | common enhanced scale',frameIndices(1)));
nexttile; imagesc(images(:,:,end),[lo max(hi,lo+eps)]); axis image; colorbar;
title(sprintf('Frame %d | common enhanced scale',frameIndices(end)));
nexttile; plot((0:numel(waveform)-1)/sampleRate*1e6,waveform); grid on;
    xlim([0 min(100,5e6/max(frameStats{1}.estimatedFrequencyHz,1))]);
xlabel('Time (us)'); ylabel('ADC code');
title(sprintf('Raw waveform | %.4f kHz',frameStats{1}.estimatedFrequencyHz/1e3));
exportgraphics(fig,[baseAll '_preview.png'],'Resolution',120); close(fig);
summary = struct('inputFile',dataFile,'fileBytes',info.bytes,'availableFrames',availableFrames, ...
    'frameIndices',frameIndices,'reconstructionSeconds',toc(startTime),'frameStats',frameStats);
jf = fopen([baseAll '_summary.json'],'w','n','UTF-8');
assert(jf>=0); fprintf(jf,'%s\n',jsonencode(summary,PrettyPrint=true)); fclose(jf);
fprintf('MULTIFRAME_RESULT frames=%d seconds=%.3f output=%s\n',numel(frameIndices),toc(startTime),baseAll);
