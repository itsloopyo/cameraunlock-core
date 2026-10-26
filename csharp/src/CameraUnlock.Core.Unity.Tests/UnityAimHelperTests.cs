using Xunit;
using UnityEngine;
using CameraUnlock.Core.Unity.Extensions;

namespace CameraUnlock.Core.Unity.Tests
{
    public class UnityAimHelperTests
    {
        // Behind the camera and to its right: the marker goes to the right edge. The x and y of
        // camera space were negated, which put it on the left, the side the aim was not.
        [Fact]
        public void ClampToScreenEdge_AnAimBehindAndRight_GoesToTheRightEdge()
        {
            var camera = new Camera();
            camera.ResetWorldToCameraMatrix();

            Vector2 offset = UnityAimHelper.ClampToScreenEdge(new Vector3(1f, 0.5f, -1f), camera, 960f, 540f, 0.9f);

            Assert.Equal(960f * 0.9f, offset.x, 3);
            Assert.Equal(540f * 0.9f * 0.5f, offset.y, 3);
        }
    }
}
