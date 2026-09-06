using SpatialLab.GlobalEnums;
using SpatialLab.Interfaces;
using SpatialLab.Models;
using SpatialLab.ViewModels;

// 가짜 서비스를 사용해 WPF 없이 ViewModel의 설정·명령·알림·종료를 검증한다.
static class Program
{
    private static int assertions;
    private static void Require(bool value,string message)
    {
        ++assertions;
        if (!value) throw new Exception(message);
    }
    public static int Main()
    {
        try
        {
            var service=new FakeSceneService();
            using var vm=new MainViewModel(service);
            var changed=new HashSet<string>();
            vm.PropertyChanged+=(_,e)=>changed.Add(e.PropertyName ?? "");
            Require(!vm.PauseCommand.CanExecute(null) && !vm.ResetCommand.CanExecute(null),"Commands before initialization");
            vm.SelectedObjectCount=5000;vm.Radius=20;
            Require(service.Calls==0,"Settings before ready reached engine");
            service.SetReady(true);
            Require(service.Calls==1 && service.LastReset && service.Last?.ObjectCount==5000 &&
                service.Last?.Radius==20,"Latest settings on attachment");
            Require(vm.PauseCommand.CanExecute(null),"Command availability notification");
            vm.PauseCommand.Execute(null);
            Require(vm.IsPaused && service.Last!.IsPaused && vm.PauseLabel=="이동 재개","Pause command");
            Require(changed.Contains(nameof(vm.PauseLabel)) && changed.Contains(nameof(vm.CanInteract)),"Dependent notifications");
            vm.SelectedDistribution=ObjectDistribution.Clustered;
            vm.SelectedSearchMethod=SearchMethod.Linear;
            vm.ShowGrid=false;
            vm.ResetCommand.Execute(null);
            Require(service.LastReset && service.Last!.Distribution==ObjectDistribution.Clustered &&
                service.Last.SearchMethod==SearchMethod.Linear && !service.Last.ShowGrid &&
                service.Last.IsPaused && service.Last.Seed==42,"Reset preserves current settings");
            var stats=new SceneStatistics(5000,7,5000,90,true,false,true,0.01,0.002,0.003,0.001,30,40);
            service.Publish(stats);
            Require(vm.Statistics==stats && vm.HitText=="7개" && vm.CandidateText.Contains("90"),"Statistics data");
            Require(vm.TotalTimeText.StartsWith("5.00") && !vm.HasMismatch && vm.StatusText.Contains("일시정지"),"Statistics presentation");
            service.Publish(stats with {ResultsMatch=false});
            Require(vm.HasMismatch && vm.MatchText=="검색 결과 불일치","Mismatch status");
            double radius=vm.Radius;
            try {vm.Radius=double.NaN;throw new Exception("NaN accepted");} catch(ArgumentOutOfRangeException) {}
            Require(vm.Radius==radius,"Invalid radius changed state");
            int count=vm.SelectedObjectCount;
            try {vm.SelectedObjectCount=42;throw new Exception("Invalid count accepted");} catch(ArgumentOutOfRangeException) {}
            Require(vm.SelectedObjectCount==count,"Invalid count changed state");
            service.ThrowOnConfigure=true;
            vm.ResetCommand.Execute(null);
            Require(vm.HasError && !vm.CanInteract && !vm.PauseCommand.CanExecute(null),"Native failure command state");
            int calls=service.Calls;
            vm.SelectedObjectCount=100;
            Require(service.Calls==calls,"Faulted VM issued native command");
            service.ThrowOnConfigure=false;
            service.SetReady(false);service.SetReady(true);
            Require(!vm.HasError && vm.CanInteract && service.Last?.ObjectCount==100,"Reattachment latest settings");
            service.Fail("device removed");
            Require(vm.StatusText.Contains("device removed") && !vm.ResetCommand.CanExecute(null),"Asynchronous rendering error");
            var before=vm.Statistics;
            vm.Dispose();
            calls=service.Calls;
            service.SetReady(true);service.Publish(stats);
            vm.SelectedObjectCount=10;
            Require(service.Calls==calls && vm.Statistics==before && !vm.PauseCommand.CanExecute(null),"Dispose unsubscription");
            Require(!typeof(MainViewModel).Assembly.GetReferencedAssemblies().Any(a=>
                a.Name is "PresentationFramework" or "PresentationCore"),"ViewModel depends on WPF");
            Console.WriteLine($"PASS: {assertions} ViewModel assertions (lifecycle, commands, notifications, failures, disposal; no WPF/DirectX).");
            return 0;
        }
        catch(Exception error) {Console.Error.WriteLine(error);return 1;}
    }
}
sealed class FakeSceneService : ISceneService
{
    public bool IsReady {get;private set;}
    public int Calls {get;private set;}
    public SceneSettings? Last {get;private set;}
    public bool LastReset {get;private set;}
    public bool ThrowOnConfigure {get;set;}
    public event EventHandler? AvailabilityChanged;
    public event EventHandler<SceneStatistics>? StatisticsUpdated;
    public event EventHandler<string>? Faulted;
    public void Configure(SceneSettings settings,bool reset)
    {
        ++Calls;
        if(ThrowOnConfigure) throw new InvalidOperationException("test configure failure");
        Last=settings;LastReset=reset;
    }
    public void SetReady(bool value) {IsReady=value;AvailabilityChanged?.Invoke(this,EventArgs.Empty);}
    public void Publish(SceneStatistics stats)=>StatisticsUpdated?.Invoke(this,stats);
    public void Fail(string message)=>Faulted?.Invoke(this,message);
}
