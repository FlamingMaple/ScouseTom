EEG=pop_loadbv;
%%
 Fs = EEG.srate;
 %Bad_channels = [2,5,6,10,11,14,17,18,20,22,24,25,28:32];
 % map_=[1:30]; % Montage
   %map_=[1 3 4 7 8 9 12 13 14 15 16 19 21 22 23 26 27 28]; % Actual numbering
 
 %Good_ch=setdiff(map,Bad_channels);
 %Good_ch=map_;
 Ref = 2;
 map = [20 22 28 14 ...
        7  17 11 31 ...
        2  6  29 32 ...
        3  5  27 30 ...
        4  24 12 15 ...
        19 1  9  10 ...
        18 8  26 16 ...
        23 21 25 13 ];
    
 map_ = [1:Ref-1,Ref+1:32];   
 
 t=cell2mat({EEG.event.latency})';
 
 jj=zeros(length(t),1);
 for i=1:length(t)
     if strcmp([EEG.event(1,i).type], ['S  2']);
       jj(i)=1;
     end
 end
 
 
 T_trig=t(jj==1);
        
 tau=30;
 size_bin=floor(tau*Fs/1000);
 
 Data= double(EEG.data');
 
 %Data=detrend(Data);
   [b,a] = butter(5,3000/(Fs/2),'low');
  Data = filtfilt(b,a,Data);
%  
 
 
%      [b,a] = iirnotch(50/(Fs/2),(50/(Fs/2))/25);
%       Data = filtfilt(b,a,Data);
%  
  [b,a] = butter(3,1/(Fs/2),'high');
  Data = filtfilt(b,a,Data);
 % Data = Data-repmat(Data(1,:),size(Data,1),1);
 
  
  T=[1:size_bin]*1000/Fs-tau/2;
 
 EP=zeros(length(T_trig)-2,size_bin,size(Data,2));
 for i=2:length(T_trig)-1
     EP(i-1,:,:)=Data([T_trig(i)-floor(size_bin/2):T_trig(i)+floor(size_bin/2)-1],:);
 end
 
%  for i=1:length(T_trig)-1
%      EP(i,:,:)=Data([T_trig(i)-floor(size_bin/2):T_trig(i)+floor(size_bin/2)-1],:);
%  end
 
 EP_avg=(squeeze(mean(EP,1))); %detrend
 EP_avg(T>=-0.5&T<=0.5,:)=0;
%    figure 
%   pwelch(EP_avg(:,2),[],[],25000,Fs);
%%
 figure;
 
 [b,a] = butter(5,5000/(Fs/2),'low');
 for i = 1:32
     k = find(map_==map(i));
     if (~isempty(k) && max(abs(EP_avg(T>0&T<0.2,k)))<10000)
         subplot(8,4,i)
         plot(T,filtfilt(b,a,EP_avg(:,k)));
         ylim([-10,10]);
         xlim([-5,15]);
     end
 end
 